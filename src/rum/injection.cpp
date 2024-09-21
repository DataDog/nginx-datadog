#include "injection.h"

extern "C" {
#include <ngx_core.h>
}

#include <cassert>

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

bool is_html_content(ngx_str_t *content_type) {
  assert(content_type != nullptr);
  std::string_view content_type_sv = to_string_view(*content_type);
  return content_type_sv.find("text/html") != content_type_sv.npos;
}

}  // namespace

InjectionHandler::InjectionHandler()
    : output_padding_(false), injector_(nullptr) {}

InjectionHandler::~InjectionHandler() {
  if (injector_ != nullptr) {
    injector_cleanup(injector_);
  }
}

ngx_int_t InjectionHandler::on_rewrite_handler(ngx_http_request_t *r) {
  ngx_table_elt_t *h =
      static_cast<ngx_table_elt_t *>(ngx_list_push(&r->headers_in.headers));
  if (h == nullptr) {
    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                  "RUM SDK injection failed: unable to add "
                  "x-datadog-rum-injection-pending HTTP header");
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
  assert(cfg->rum_snippet != nullptr);

  if (!cfg->rum_enable) {
    return next_header_filter(r);
  }

  if (auto injected_header = search_header(r, "x-datadog-rum-injected");
      injected_header != nullptr) {
    if (nginx::to_string_view(injected_header->value) == "1") {
      ngx_log_error(NGX_LOG_INFO, r->connection->log, 0,
                    "RUM SDK injection skipped: resource may already have RUM "
                    "SDK injected.");
      return next_header_filter(r);
    }
  }

  if (r->header_only || r->headers_out.content_length_n == 0) {
    ngx_log_error(NGX_LOG_INFO, r->connection->log, 0,
                  "RUM SDK injection skipped: empty content");
    return next_header_filter(r);
  }

  if (!is_html_content(&r->headers_out.content_type)) {
    ngx_log_error(NGX_LOG_INFO, r->connection->log, 0,
                  "RUM SDK injection skipped: not an HTML page");
    return next_header_filter(r);
  }

  if (auto content_encoding = r->headers_out.content_encoding;
      content_encoding != nullptr && content_encoding->value.len != 0) {
    ngx_log_error(NGX_LOG_INFO, r->connection->log, 0,
                  "RUM SDK injection skipped: compressed html content");
    return next_header_filter(r);
  }

  state_ = state::searching;
  injector_ = injector_create(cfg->rum_snippet);

  // In case `Transfer-Encoding: chunk` is enabled no need to update the
  // content length.
  if (r->headers_out.content_length_n != -1) {
    output_padding_ = true;
    r->headers_out.content_length_n += cfg->rum_snippet->length;
  }

  // Set header now because it will be too late after
  // TODO(@dmehala): write common function to insert HTTP Header
  auto *h =
      static_cast<ngx_table_elt_t *>(ngx_list_push(&r->headers_out.headers));
  if (h == nullptr) {
    state_ = state::error;
    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                  "RUM SDK injection failed: unable to add "
                  "x-datadog-sdk-injected HTTP header");
    return NGX_ERROR;
  }

  h->hash = 1;
  ngx_str_set(&h->key, "x-datadog-sdk-injected");
  ngx_str_set(&h->value, "1");

  // If `filter_need_in_memory` is not set, the filter can be called on with a
  // buffer a file. The following explicitly ask for the buffer to be in memory,
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

  ngx_chain_t *output_chain;
  ngx_chain_t *previous_chain = nullptr;
  ngx_chain_t **current_chain = &output_chain;

  for (ngx_chain_t *cl = in; cl; cl = cl->next) {
    uint32_t buffer_size = cl->buf->last - cl->buf->pos;
    auto result = injector_write(
        injector_, static_cast<uint8_t *>(cl->buf->pos), buffer_size);

    ngx_chain_t *injected_cl =
        inject(r->pool, cl, std::span(result.slices, result.slices_length));

    previous_chain = cl;
    *current_chain = injected_cl;
    current_chain = &injected_cl->next;

    if (result.injected) {
      state_ = state::injected;
      ngx_log_error(NGX_LOG_INFO, r->connection->log, 0,
                    "RUM SDK injected successfully injected");
      return output(r, output_chain, next_body_filter);
    }
  }

  if (previous_chain->buf->last_buf && output_padding_) {
    state_ = state::failed;
    auto result = injector_end(injector_);
    ngx_chain_t *injected_cl =
        inject(r->pool, previous_chain,
               std::span(result.slices, result.slices_length));

    *current_chain = injected_cl;
    current_chain = &injected_cl->next;

    ngx_log_error(NGX_LOG_INFO, r->connection->log, 0,
                  "RUM SDK injection failed: no injection point found");
  }

  return output(r, output_chain, next_body_filter);
}

// NOTE(@dmehala): this function is not necessary for now, however,
// it will when we will reuse buffer.
ngx_int_t InjectionHandler::output(
    ngx_http_request_t *r, ngx_chain_t *out,
    ngx_http_output_body_filter_pt &next_body_filter) {
  return next_body_filter(r, out);
}

// NOTE(@dmehala): Ideally for v2 the buffer should be reuse to avoid
// unnecessary allocation.
ngx_chain_t *InjectionHandler::inject(ngx_pool_t *pool, ngx_chain_t *in,
                                      std::span<const BytesSlice> slices) {
  assert(pool != nullptr);
  assert(in != nullptr);
  if (slices.empty()) {
    return in;
  }

  size_t needed = 0;
  for (uint32_t i = 0; i < slices.size(); ++i) {
    needed += slices[i].length;
  }

  ngx_chain_t *cl = ngx_alloc_chain_link(pool);
  if (cl == nullptr) {
    // NOTE(@dmehala): This might explain why we couldn't inject the SDK.
    // It should stop looking for an injection point and report the injection
    // as failed.
    ngx_log_error(NGX_LOG_ERR, pool->log, 0,
                  "RUM SDK injection failed: insufficient memory available");
    return in;
  }

  ngx_buf_t *buf = (ngx_buf_t *)ngx_calloc_buf(pool);

  buf->tag = (ngx_buf_tag_t)&ngx_http_datadog_module;
  buf->memory = 1;
  buf->start = (u_char *)ngx_pnalloc(pool, needed);
  buf->end = buf->start + needed;
  buf->pos = buf->start;
  buf->last = buf->pos;
  buf->flush = in->buf->flush;
  buf->sync = in->buf->sync;
  buf->last_buf = in->buf->last_buf;
  buf->last_in_chain = in->buf->last_in_chain;

  for (uint32_t i = 0; i < slices.size(); ++i) {
    buf->last = ngx_cpymem(buf->last, slices[i].start, slices[i].length);
  }

  cl->buf = buf;
  cl->next = in->next;
  return cl;
}

}  // namespace rum
}  // namespace nginx
}  // namespace datadog
