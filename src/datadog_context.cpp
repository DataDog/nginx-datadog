#include "datadog_context.h"

#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <string_view>

#include "datadog/span.h"
#include "datadog_handler.h"
#include "dd.h"
#include "ngx_header_writer.h"
#include "ngx_http_datadog_module.h"
#ifdef WITH_WAF
#include "security/context.h"
#endif
#include "string_util.h"

namespace datadog {
namespace nginx {
namespace {

#ifdef WITH_WAF
bool is_apm_tracing_enabled(ngx_http_request_t *request) {
  auto main_conf = static_cast<datadog_main_conf_t *>(
      ngx_http_get_module_main_conf(request, ngx_http_datadog_module));
  if (main_conf == nullptr) return false;

  return main_conf->apm_tracing_enabled;
}
#endif

}  // namespace

DatadogContext::DatadogContext(ngx_http_request_t *request,
                               ngx_http_core_loc_conf_t *core_loc_conf,
                               datadog_loc_conf_t *loc_conf)
#ifdef WITH_WAF
    : sec_ctx_{security::Context::maybe_create(
          security::Library::max_saved_output_data(),
          is_apm_tracing_enabled(request))}
#endif
{
  if (loc_conf->enable_tracing) {
    traces_.emplace_back(request, core_loc_conf, loc_conf);
  }

#ifdef WITH_RUM
  if (loc_conf->rum_enable) {
    auto *trace = find_trace(request);
    if (trace != nullptr) {
      auto rum_span = trace->active_span().create_child();
      rum_span.set_name("rum_sdk_injection.on_rewrite_handler");
      auto status = rum_ctx_.on_rewrite_handler(request);
      if (status == NGX_ERROR) {
        rum_span.set_error(true);
      }
    } else {
      rum_ctx_.on_rewrite_handler(request);
    }
  }
#endif
}

void DatadogContext::on_change_block(ngx_http_request_t *request,
                                     ngx_http_core_loc_conf_t *core_loc_conf,
                                     datadog_loc_conf_t *loc_conf) {
  if (loc_conf->enable_tracing) {
    auto trace = find_trace(request);
    if (trace != nullptr) {
      trace->on_change_block(core_loc_conf, loc_conf);
    } else {
      // This is a new subrequest, so add a RequestTracing for it.
      // TODO: Should `active_span` be `request_span` instead?
      traces_.emplace_back(request, core_loc_conf, loc_conf,
                           &traces_[0].active_span());
    }
  }
}

#ifdef WITH_WAF
bool DatadogContext::on_main_req_access(ngx_http_request_t *request) {
  if (!sec_ctx_) {
    return false;
  }

  // there should only one trace at this point
  dd::Span &span = single_trace().active_span();
  return sec_ctx_->on_request_start(*request, span);
}
#endif

ngx_int_t DatadogContext::on_header_filter(ngx_http_request_t *request) {
  auto *loc_conf = static_cast<datadog_loc_conf_t *>(
      ngx_http_get_module_loc_conf(request, ngx_http_datadog_module));
  if (loc_conf == nullptr) {
    ngx_log_error(NGX_LOG_ERR, request->connection->log, 0,
                  "on_header_filter failed: could not get loc conf");
    return ngx_http_next_header_filter(request);
  }

#if defined(WITH_RUM) || defined(WITH_WAF)
  RequestTracing *trace{};
#endif
#ifdef WITH_RUM
  if (loc_conf->rum_enable) {
    trace = find_trace(request);
    if (trace != nullptr) {
      auto rum_span = trace->active_span().create_child();
      rum_span.set_name("rum_sdk_injection.on_header");
      auto status = rum_ctx_.on_header_filter(request, loc_conf,
                                              ngx_http_next_header_filter);
      if (status == NGX_ERROR) {
        rum_span.set_error(true);
      }
      return status;
    } else {
      return rum_ctx_.on_header_filter(request, loc_conf,
                                       ngx_http_next_header_filter);
    }
  }
#elif WITH_WAF
  if (sec_ctx_) {
    trace = find_trace(request);
  }
#endif

#ifdef WITH_WAF
  if (sec_ctx_ && trace) {
    dd::Span &span = trace->active_span();
    return sec_ctx_->header_filter(*request, span);
  }
#endif

  return ngx_http_next_header_filter(request);
}

#ifdef WITH_WAF
ngx_int_t DatadogContext::request_body_filter(ngx_http_request_t *request,
                                              ngx_chain_t *chain) {
  if (!sec_ctx_) {
    return ngx_http_next_request_body_filter(request, chain);
  }

  auto *trace = find_trace(request);
  if (trace == nullptr) {
    throw std::runtime_error{
        "request_body_filter: could not find request trace"};
  }

  dd::Span &span = trace->active_span();
  return sec_ctx_->request_body_filter(*request, chain, span);
}
#endif

ngx_int_t DatadogContext::on_output_body_filter(ngx_http_request_t *request,
                                                ngx_chain_t *chain) {
  auto *loc_conf = static_cast<datadog_loc_conf_t *>(
      ngx_http_get_module_loc_conf(request, ngx_http_datadog_module));
  if (loc_conf == nullptr) {
    ngx_log_error(NGX_LOG_ERR, request->connection->log, 0,
                  "on_output_body_filter failed: could not get loc conf");
    return ngx_http_next_output_body_filter(request, chain);
  }
#ifdef WITH_WAF
  if (!sec_ctx_) {
    return ngx_http_next_output_body_filter(request, chain);
  }

  auto *trace = find_trace(request);
  if (trace == nullptr) {
    throw std::runtime_error{
        "main_output_body_filter: could not find request trace"};
  }

  dd::Span &span = trace->active_span();
  return sec_ctx_->output_body_filter(*request, chain, span);
#endif

#ifdef WITH_RUM
  // TODO: If WAF is blocking, no need to inject the RUM SDK.
  if (loc_conf->rum_enable) {
    auto *trace = find_trace(request);
    if (trace != nullptr) {
      auto rum_span = trace->active_span().create_child();
      rum_span.set_name("rum_sdk_injection.on_body_filter");
      rum_span.set_tag("configuration.length",
                       std::to_string(loc_conf->rum_snippet->length));
      auto status = rum_ctx_.on_body_filter(request, loc_conf, chain,
                                            ngx_http_next_output_body_filter);
      if (status == NGX_ERROR) {
        rum_span.set_error(true);
      }
      return status;
    } else {
      return rum_ctx_.on_body_filter(request, loc_conf, chain,
                                     ngx_http_next_output_body_filter);
    }
  }
#endif

  return ngx_http_next_output_body_filter(request, chain);
}

void DatadogContext::on_log_request(ngx_http_request_t *request) {
  auto *loc_conf = static_cast<datadog_loc_conf_t *>(
      ngx_http_get_module_loc_conf(request, ngx_http_datadog_module));
  if (loc_conf == nullptr) {
    throw std::runtime_error{"on_log_request failed: could not get loc conf"};
  }

#ifdef WITH_RUM
  if (loc_conf->rum_enable) {
    rum_ctx_.on_log_request(request);
  }
#endif

  if (!loc_conf->enable_tracing) {
    return;
  }

  auto trace = find_trace(request);
  if (trace == nullptr) {
    throw std::runtime_error{
        "on_log_request failed: could not find request trace"};
  }
  trace->on_log_request();

#ifdef WITH_WAF
  if (sec_ctx_ && request == request->main) {
    sec_ctx_->on_main_log_request(*request, trace->active_span());
  }
#endif
}

ngx_str_t DatadogContext::lookup_span_variable_value(
    ngx_http_request_t *request, std::string_view key) {
  auto trace = find_trace(request);
  if (trace == nullptr) {
    throw std::runtime_error{
        "lookup_span_variable_value failed: could not find request trace"};
  }
  return trace->lookup_span_variable_value(key);
}

RequestTracing *DatadogContext::find_trace(ngx_http_request_t *request) {
  const auto found = std::find_if(
      traces_.begin(), traces_.end(),
      [=](const auto &trace) { return trace.request() == request; });
  if (found != traces_.end()) {
    return &*found;
  }
  return nullptr;
}

RequestTracing &DatadogContext::single_trace() {
  if (traces_.size() != 1) {
    throw std::runtime_error{"Expected there to be exactly one trace"};
  }
  return traces_[0];
}

const RequestTracing *DatadogContext::find_trace(
    ngx_http_request_t *request) const {
  return const_cast<DatadogContext *>(this)->find_trace(request);
}

static void cleanup_datadog_context(void *data) noexcept {
  delete static_cast<DatadogContext *>(data);
}

static ngx_pool_cleanup_t *find_datadog_cleanup(ngx_http_request_t *request) {
  for (auto cleanup = request->pool->cleanup; cleanup;
       cleanup = cleanup->next) {
    if (cleanup->handler == cleanup_datadog_context) {
      return cleanup;
    }
  }
  return nullptr;
}

DatadogContext *get_datadog_context(ngx_http_request_t *request) noexcept {
  auto context = static_cast<DatadogContext *>(
      ngx_http_get_module_ctx(request, ngx_http_datadog_module));
  if (context != nullptr || !request->internal) {
    return context;
  }

  // If this is an internal redirect, the DatadogContext will have been
  // reset, but we can still recover it from the cleanup handler.
  //
  // See set_datadog_context below.
  auto cleanup = find_datadog_cleanup(request);
  if (cleanup != nullptr) {
    context = static_cast<DatadogContext *>(cleanup->data);
  }

  // If we found a context, attach with ngx_http_set_ctx so that we don't have
  // to loop through the cleanup handlers again.
  if (context != nullptr) {
    ngx_http_set_ctx(request, static_cast<void *>(context),
                     ngx_http_datadog_module);
  }

  return context;
}

// Attaches an DatadogContext to a request.
//
// Note that internal redirects for nginx will clear any data attached via
// ngx_http_set_ctx. Since DatadogContext needs to persist across
// redirection, as a workaround the context is stored in a cleanup handler where
// it can be later recovered.
//
// See the discussion in
//    https://forum.nginx.org/read.php?29,272403,272403#msg-272403
// or the approach taken by the standard nginx realip module.
void set_datadog_context(ngx_http_request_t *request, DatadogContext *context) {
  auto cleanup = ngx_pool_cleanup_add(request->pool, 0);
  if (cleanup == nullptr) {
    delete context;
    throw std::runtime_error{"failed to allocate cleanup handler"};
  }
  cleanup->data = static_cast<void *>(context);
  cleanup->handler = cleanup_datadog_context;
  ngx_http_set_ctx(request, static_cast<void *>(context),
                   ngx_http_datadog_module);
}

// Supports early destruction of the DatadogContext (in case of an
// unrecoverable error).
void destroy_datadog_context(ngx_http_request_t *request) noexcept {
  auto cleanup = find_datadog_cleanup(request);
  if (cleanup == nullptr) {
    ngx_log_error(NGX_LOG_ERR, request->connection->log, 0,
                  "Unable to find Datadog cleanup handler for request %p",
                  request);
    return;
  }
  delete static_cast<DatadogContext *>(cleanup->data);
  cleanup->data = nullptr;
  ngx_http_set_ctx(request, nullptr, ngx_http_datadog_module);
}

ngx_int_t DatadogContext::on_precontent_phase(ngx_http_request_t *request) {
  // When tracing is disabled (e.g. `datadog_tracing off`), no traces are
  // created. Skip header injection to avoid accessing an empty container.
  if (traces_.empty()) {
    return NGX_DECLINED;
  }

  // inject headers in the precontent phase into the request headers
  // These headers will be copied by ngx_http_proxy_create_request on the
  // content phase into the outgoing request headers (probably)
  RequestTracing &trace = traces_.front();
  dd::Span &span = trace.active_span();
  span.set_tag("span.kind", "client");

#ifdef WITH_WAF
  if (auto sec_ctx = get_security_context()) {
    if (sec_ctx->keep_span()) {
      span.set_source(datadog::tracing::Source::appsec);
    }
  }
#endif

  NgxHeaderWriter writer(request);
  span.inject(writer);

  return NGX_DECLINED;
}

}  // namespace nginx
}  // namespace datadog
