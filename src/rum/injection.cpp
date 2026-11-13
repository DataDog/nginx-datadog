#include "injection.h"

#include <datadog/telemetry/telemetry.h>

extern "C" {
#include <ngx_core.h>
}

#include <cassert>

#include "common/headers.h"
#include "datadog_conf.h"
#include "ngx_http_datadog_module.h"
#include "string_util.h"
#include "telemetry.h"

namespace datadog {
namespace nginx {
namespace rum {
namespace {

bool is_html_content(ngx_str_t *content_type) {
  assert(content_type != nullptr);
  std::string_view content_type_sv = to_string_view(*content_type);
  return content_type_sv.find("text/html") != content_type_sv.npos;
}

// Appends a buffer with the bytes of `result` to the chain ending at `tail`.
// It takes the flags of `flags_from`, if any. Adds nothing when there are no
// bytes and no flags. Returns the new tail, or nullptr if out of memory.
ngx_chain_t **append(ngx_pool_t *pool, ngx_chain_t **tail,
                     const InjectorResult &result,
                     const ngx_buf_t *flags_from) {
  size_t size = 0;
  for (uint32_t i = 0; i < result.slices_length; ++i) {
    size += result.slices[i].length;
  }
  // nginx rejects empty buffers that are not special, i.e. without flags.
  bool has_flags = flags_from != nullptr &&
                   (flags_from->flush || flags_from->sync ||
                    flags_from->last_buf || flags_from->last_in_chain);
  if (size == 0 && !has_flags) {
    return tail;
  }

  ngx_chain_t *cl = ngx_alloc_chain_link(pool);
  auto *buf = static_cast<ngx_buf_t *>(ngx_calloc_buf(pool));
  u_char *data =
      size == 0 ? nullptr : static_cast<u_char *>(ngx_pnalloc(pool, size));
  if (cl == nullptr || buf == nullptr || (size > 0 && data == nullptr)) {
    ngx_log_error(NGX_LOG_ERR, pool->log, 0,
                  "RUM SDK injection failed: insufficient memory available");
    return nullptr;
  }
  if (size > 0) {
    buf->start = data;
    buf->end = buf->start + size;
    buf->pos = buf->start;
    buf->last = buf->start;
    buf->memory = 1;
    for (uint32_t i = 0; i < result.slices_length; ++i) {
      buf->last = ngx_cpymem(buf->last, result.slices[i].start,
                             result.slices[i].length);
    }
  }
  buf->tag = (ngx_buf_tag_t)&ngx_http_datadog_module;
  if (flags_from != nullptr) {
    buf->flush = flags_from->flush;
    buf->sync = flags_from->sync;
    buf->last_buf = flags_from->last_buf;
    buf->last_in_chain = flags_from->last_in_chain;
  }

  cl->buf = buf;
  cl->next = nullptr;
  *tail = cl;
  return &cl->next;
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
  if (!common::add_header(*r->pool, r->headers_in.headers,
                          "x-datadog-rum-injection-pending", "1")) {
    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                  "RUM SDK injection failed: unable to add "
                  "x-datadog-rum-injection-pending HTTP header");
    return NGX_ERROR;
  }

  return NGX_DECLINED;
}

ngx_int_t InjectionHandler::on_header_filter(
    ngx_http_request_t *r, datadog_loc_conf_t *cfg,
    ngx_http_output_header_filter_pt &next_header_filter) {
  assert(cfg->rum_snippet != nullptr);

  if (!cfg->rum_enable) {
    return next_header_filter(r);
  }

  if (auto injected_header = common::search_header(r->headers_in.headers,
                                                   "x-datadog-rum-injected");
      injected_header != nullptr) {
    if (nginx::to_string_view(injected_header->value) == "1") {
      ngx_log_error(NGX_LOG_INFO, r->connection->log, 0,
                    "RUM SDK injection skipped: resource may already have RUM "
                    "SDK injected.");
      datadog::telemetry::counter::increment(
          telemetry::injection_skipped,
          telemetry::build_tags("reason:already_injected",
                                cfg->rum_application_id_tag,
                                cfg->rum_remote_config_tag));

      return next_header_filter(r);
    }
  }

  if (r->header_only || r->headers_out.content_length_n == 0) {
    ngx_log_error(NGX_LOG_INFO, r->connection->log, 0,
                  "RUM SDK injection skipped: empty content");

    datadog::telemetry::counter::increment(
        telemetry::injection_skipped,
        telemetry::build_tags("reason:no_content", cfg->rum_application_id_tag,
                              cfg->rum_remote_config_tag));

    return next_header_filter(r);
  }

  if (!is_html_content(&r->headers_out.content_type)) {
    ngx_log_error(NGX_LOG_INFO, r->connection->log, 0,
                  "RUM SDK injection skipped: not an HTML page");

    datadog::telemetry::counter::increment(
        telemetry::injection_skipped,
        telemetry::build_tags("reason:invalid_content_type",
                              cfg->rum_application_id_tag,
                              cfg->rum_remote_config_tag));

    return next_header_filter(r);
  }

  if (auto content_encoding = r->headers_out.content_encoding;
      content_encoding != nullptr && content_encoding->value.len != 0) {
    ngx_log_error(NGX_LOG_INFO, r->connection->log, 0,
                  "RUM SDK injection skipped: compressed html content");

    datadog::telemetry::counter::increment(
        telemetry::injection_skipped,
        telemetry::build_tags("reason:compressed_html",
                              cfg->rum_application_id_tag,
                              cfg->rum_remote_config_tag));

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
                  "x-datadog-rum-injected HTTP header");
    return NGX_ERROR;
  }

  h->hash = 1;
  ngx_str_set(&h->key, "x-datadog-rum-injected");
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

  // The injector may hold bytes back, so each buffer is replaced by what it
  // returns. The held bytes come back in later results, or at the end.
  ngx_chain_t *out = nullptr;
  ngx_chain_t **tail = &out;
  for (ngx_chain_t *cl = in; cl; cl = cl->next) {
    ngx_buf_t *buf = cl->buf;
    auto result = injector_write(injector_, static_cast<uint8_t *>(buf->pos),
                                 buf->last - buf->pos);
    buf->pos = buf->last;
    bool injected = result.injected;
    bool last = buf->last_buf;

    // On the last buffer, the flags go after what the end returns.
    tail = append(r->pool, tail, result, last ? nullptr : buf);
    if (tail == nullptr) {
      return NGX_ERROR;
    }
    if (last) {
      auto ended = output_padding_ ? injector_end(injector_)
                                   : injector_end_without_padding(injector_);
      injected = injected || ended.injected;
      tail = append(r->pool, tail, ended, buf);
      if (tail == nullptr) {
        return NGX_ERROR;
      }
    }

    if (injected) {
      *tail = cl->next;
      state_ = state::injected;
      ngx_log_error(NGX_LOG_INFO, r->connection->log, 0,
                    "RUM SDK injected successfully injected");
      datadog::telemetry::counter::increment(
          telemetry::injection_succeed,
          telemetry::build_tags(cfg->rum_application_id_tag,
                                cfg->rum_remote_config_tag));
      break;
    }
    if (last) {
      state_ = state::failed;
      ngx_log_error(NGX_LOG_INFO, r->connection->log, 0,
                    "RUM SDK injection failed: no injection point found");
      datadog::telemetry::counter::increment(
          telemetry::injection_failed,
          telemetry::build_tags("reason:missing_header_tag",
                                cfg->rum_application_id_tag,
                                cfg->rum_remote_config_tag));
    }
  }

  return output(r, out, next_body_filter);
}

ngx_int_t InjectionHandler::on_log_request(ngx_http_request_t *r) {
  if (auto csp = common::search_header(r->headers_out.headers,
                                       "content-security-policy");
      csp != nullptr) {
    datadog::telemetry::counter::increment(
        telemetry::injection_failed,
        telemetry::build_tags("status:seen", "kind:header"));
  }

  return NGX_OK;
}

// NOTE(@dmehala): this function is not necessary for now, however,
// it will when we will reuse buffer.
ngx_int_t InjectionHandler::output(
    ngx_http_request_t *r, ngx_chain_t *out,
    ngx_http_output_body_filter_pt &next_body_filter) {
  return next_body_filter(r, out);
}

}  // namespace rum
}  // namespace nginx
}  // namespace datadog
