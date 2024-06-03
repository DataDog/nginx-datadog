#include "injection.h"

extern "C" {
#include <ngx_core.h>
}

#include "datadog_conf.h"
#include "ngx_http_datadog_module.h"
#include "string_util.h"

namespace datadog {
namespace nginx {
namespace rum {
namespace {

ngx_table_elt_t *search_header(ngx_http_request_t *request,
                               std::string_view key) {
  ngx_list_part_t *part = &request->headers_in.headers.part;
  auto *h = static_cast<ngx_table_elt_t *>(part->elts);

  for (std::size_t i = 0;; i++) {
    if (i >= part->nelts) {
      if (part->next == nullptr) {
        break;
      }

      part = part->next;
      h = static_cast<ngx_table_elt_t *>(part->elts);
      i = 0;
    }

    if (key.size() != h[i].key.len ||
        ngx_strcasecmp((u_char *)key.data(), h[i].key.data) != 0) {
      continue;
    }

    return &h[i];
  }

  return nullptr;
}

ngx_int_t dd_validate_content_type(ngx_str_t *content_type) {
  std::string_view content_type_sv = to_string_view(*content_type);
  return content_type_sv.find("text/html") != content_type_sv.npos;
}

// TODO: Add support for other `Content-Encoding`.
// More details: https://en.wikipedia.org/wiki/HTTP_compression
ngx_int_t dd_content_compressed(ngx_http_headers_out_t *headers) {
  return (headers->content_encoding != nullptr &&
          headers->content_encoding->value.len != 4 &&
          ngx_strncasecmp(headers->content_encoding->value.data,
                          (u_char *)"gzip", 4) == 0);
}
}  // namespace

InjectionHandler::InjectionHandler()
    : output_padding_(false),
      busy_(nullptr),
      free_(nullptr),
      injector_(nullptr) {}

InjectionHandler::~InjectionHandler() {
  if (injector_ != nullptr) {
    injector_cleanup(injector_);
  }
}

ngx_int_t InjectionHandler::on_rewrite_handler(ngx_http_request_t *r) {
  ngx_table_elt_t *h =
      static_cast<ngx_table_elt_t *>(ngx_list_push(&r->headers_in.headers));
  if (h == nullptr) {
    return NGX_ERROR;
  }

  ngx_str_set(&h->key, "x-datadog-rum-injection-pending");
  ngx_str_set(&h->value, "1");
  h->lowcase_key = h->key.data;
  h->hash = 1;

  return NGX_DECLINED;
}

ngx_int_t InjectionHandler::on_header_filter(
    ngx_http_request_t *r, datadog_loc_conf_t *cfg,
    ngx_http_output_header_filter_pt &next_header_filter) {
  if (!cfg->rum_enable || cfg->rum_snippet == nullptr)
    return next_header_filter(r);

  if (auto injected_header = search_header(r, "x-datadog-rum-injected");
      injected_header != nullptr) {
    if (nginx::to_string_view(injected_header->value) == "1") {
      return next_header_filter(r);
    }
  }

  if (r->header_only || r->headers_out.content_length_n == 0 ||
      dd_validate_content_type(&r->headers_out.content_type) == 0)
    return next_header_filter(r);

  if (dd_content_compressed(&r->headers_out)) {
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                   "could not inject in compressed html content");
    return next_header_filter(r);
  }

  state_ = state::searching;
  injector_ = injector_create(cfg->rum_snippet);

  // In case `Transfer-Encoding: chunk` is enabled no need to update the content
  // length.
  if (r->headers_out.content_length_n != -1) {
    output_padding_ = true;
    r->headers_out.content_length_n += cfg->rum_snippet->length;
  }

  // Set header now 'cause it will be too late after
  auto *h =
      static_cast<ngx_table_elt_t *>(ngx_list_push(&r->headers_out.headers));
  if (h == nullptr) {
    state_ = state::error;
    return NGX_ERROR;
  }

  h->hash = 1;
  ngx_str_set(&h->key, "x-datadog-sdk-injected");
  ngx_str_set(&h->value, "1");

  // If `filter_need_in_memory` is not set, the filter can be called on with a
  // buffer a file. The following explicitly ask for the buffer to in memory,
  // thus after the file has been read by `ngx_http_copy_filter_module`.
  r->filter_need_in_memory = 1;

  return NGX_OK;
}

ngx_int_t InjectionHandler::on_body_filter(
    ngx_http_request_t *r, datadog_loc_conf_t *cfg, ngx_chain_t *in,
    ngx_http_output_body_filter_pt &next_body_filter) {
  if (!cfg->rum_enable || in == nullptr || state_ != state::searching) {
    return next_body_filter(r, in);
  }

  ngx_chain_t *out;
  ngx_chain_t *lp = nullptr;
  ngx_chain_t **ll = &out;
  InjectorResult result;

  for (ngx_chain_t *cl = in; cl; cl = cl->next) {
    lp = cl;

    result = injector_write(injector_, (uint8_t *)cl->buf->pos,
                            (uint32_t)(cl->buf->last - cl->buf->pos));

    // TODO: It seems the `injector` always return something even if it did not
    // found an injection point. This results in unncessary copy.
    if (result.slices_length == 0) {
      *ll = cl;
      ll = &cl->next;
    } else {
      ngx_chain_t *new_cl =
          inject(r->pool, cl, result.slices, result.slices_length);

      *ll = new_cl;
      ll = &new_cl->next;

      if (result.injected) {
        state_ = state::injected;
        return output(r, out, next_body_filter);
      }
    }
  }

  // No need for padding -> no need to call `injector_end`.
  if (output_padding_ && lp->buf->last_buf) {
    result = injector_end(injector_);
    *ll = inject(r->pool, lp, result.slices, result.slices_length);
  }

  return output(r, out, next_body_filter);
}

ngx_int_t InjectionHandler::output(
    ngx_http_request_t *r, ngx_chain_t *out,
    ngx_http_output_body_filter_pt &next_body_filter) {
  ngx_int_t rc = next_body_filter(r, out);
  ngx_chain_update_chains(r->pool, &free_, &busy_, &out,
                          (ngx_buf_tag_t)&ngx_http_datadog_module);
  return rc;
}

ngx_chain_t *InjectionHandler::inject(ngx_pool_t *pool, ngx_chain_t *in,
                                      const BytesSlice *slices,
                                      uint32_t slices_length) {
  ngx_chain_t *out;
  ngx_chain_t **ll = &out;

  ngx_chain_t *cl = ngx_chain_get_free_buf(pool, &(free_));
  if (cl == nullptr) {
    /* TBD */
  }
  size_t needed = 0;
  for (uint32_t i = 0; i < slices_length; ++i) {
    needed += slices[i].length;
  }

  ngx_buf_t *buf = cl->buf;
  ngx_memzero(buf, sizeof(ngx_buf_t));
  buf->tag = (ngx_buf_tag_t)&ngx_http_datadog_module;
  buf->memory = 1;
  buf->recycled = 1;

  buf->start = (u_char *)ngx_pnalloc(pool, sizeof(char) * needed);
  buf->end = buf->start + needed;
  buf->pos = buf->start;
  buf->last = buf->pos;

  for (uint32_t i = 0; i < slices_length; ++i) {
    ngx_cpymem(buf->last, slices[i].start, slices[i].length);
    buf->last += slices[i].length;
  }

  // TODO: use `in` last_buffer and other thingy
  cl->next = in->next;

  *ll = cl;

  if (in->buf->recycled) {
    in->buf->pos = in->buf->last;
  }

  return out;
}

}  // namespace rum
}  // namespace nginx
}  // namespace datadog
