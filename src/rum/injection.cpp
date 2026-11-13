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

// Appends `buf` itself, without a copy, to the chain ending at `tail`. Returns
// the new tail, or nullptr if out of memory.
ngx_chain_t **append_as_is(ngx_pool_t *pool, ngx_chain_t **tail,
                           ngx_buf_t *buf) {
  ngx_chain_t *cl = ngx_alloc_chain_link(pool);
  if (cl == nullptr) {
    return nullptr;
  }
  cl->buf = buf;
  cl->next = nullptr;
  *tail = cl;
  return &cl->next;
}

size_t total_length(const InjectorResult &result) {
  size_t length = 0;
  for (uint32_t i = 0; i < result.slices_length; ++i) {
    length += result.slices[i].length;
  }
  return length;
}

// Appends a buffer with a copy of the bytes of `result` to the chain ending
// at `tail`. It takes the flags of `flags_from`, if any. Adds nothing when
// there are no bytes and no special flag. Returns the new tail, or nullptr if
// out of memory.
ngx_chain_t **append_copy(ngx_pool_t *pool, ngx_chain_t **tail,
                          const InjectorResult &result,
                          const ngx_buf_t *flags_from) {
  size_t length = total_length(result);
  // nginx rejects an empty buffer unless it is special (see ngx_buf_special).
  // last_in_chain alone does not make a buffer special.
  bool special =
      flags_from != nullptr &&
      (flags_from->flush || flags_from->sync || flags_from->last_buf);
  if (length == 0 && !special) {
    return tail;
  }

  ngx_buf_t *buf = length == 0 ? static_cast<ngx_buf_t *>(ngx_calloc_buf(pool))
                               : ngx_create_temp_buf(pool, length);
  if (buf == nullptr) {
    return nullptr;
  }
  if (length > 0) {
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
  return append_as_is(pool, tail, buf);
}

}  // namespace

InjectionHandler::InjectionHandler()
    : has_content_length_(false), injector_(nullptr) {}

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
    has_content_length_ = true;
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

  ngx_chain_t *out = nullptr;
  ngx_chain_t **tail = &out;
  for (ngx_chain_t *cl = in; cl; cl = cl->next) {
    bool injected = false;
    // Once the injector gives up, it returns the bytes as they are, so the
    // buffer goes out without a copy. The last buffer still goes to the
    // injector, to end it.
    if (cl->buf->last_buf || injector_is_pending(injector_)) {
      tail = write_to_injector(r->pool, tail, cl->buf, injected);
    } else {
      tail = append_as_is(r->pool, tail, cl->buf);
    }
    if (tail == nullptr) {
      ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                    "RUM SDK injection failed: insufficient memory available");
      return NGX_ERROR;
    }
    if (injected) {
      *tail = cl->next;
      report_injected(r, cfg);
      break;
    }
    if (cl->buf->last_buf) {
      report_failed(r, cfg);
    }
  }

  return next_body_filter(r, out);
}

ngx_chain_t **InjectionHandler::write_to_injector(ngx_pool_t *pool,
                                                  ngx_chain_t **tail,
                                                  ngx_buf_t *buf,
                                                  bool &injected) {
  auto result = injector_write(injector_, static_cast<uint8_t *>(buf->pos),
                               buf->last - buf->pos);
  buf->pos = buf->last;
  injected = result.injected;
  bool last = buf->last_buf;

  // `result` points to injector memory that the next injector call
  // overwrites, so `append_copy` copies it. On the last buffer, the flags go
  // after what the end returns.
  tail = append_copy(pool, tail, result, last ? nullptr : buf);
  if (tail == nullptr || !last) {
    return tail;
  }
  // With a Content-Length, padding keeps the announced length when the
  // snippet is not injected.
  auto ended = has_content_length_ ? injector_end(injector_)
                                   : injector_end_without_padding(injector_);
  injected = injected || ended.injected;
  return append_copy(pool, tail, ended, buf);
}

void InjectionHandler::report_injected(ngx_http_request_t *r,
                                       datadog_loc_conf_t *cfg) {
  state_ = state::injected;
  ngx_log_error(NGX_LOG_INFO, r->connection->log, 0,
                "RUM SDK injected successfully");
  datadog::telemetry::counter::increment(
      telemetry::injection_succeed,
      telemetry::build_tags(cfg->rum_application_id_tag,
                            cfg->rum_remote_config_tag));
}

void InjectionHandler::report_failed(ngx_http_request_t *r,
                                     datadog_loc_conf_t *cfg) {
  state_ = state::failed;
  ngx_log_error(NGX_LOG_INFO, r->connection->log, 0,
                "RUM SDK injection failed: no injection point found");
  datadog::telemetry::counter::increment(
      telemetry::injection_failed,
      telemetry::build_tags("reason:missing_header_tag",
                            cfg->rum_application_id_tag,
                            cfg->rum_remote_config_tag));
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

}  // namespace rum
}  // namespace nginx
}  // namespace datadog
