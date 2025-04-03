#include "datadog_handler.h"

#include <datadog/telemetry/telemetry.h>

#include <exception>

#include "datadog_context.h"
#include "ngx_http_datadog_module.h"
#include "telemetry_util.h"

extern "C" {
#include <ngx_config.h>
extern ngx_module_t ngx_http_datadog_module;
}

namespace datadog {
namespace nginx {

ngx_http_output_header_filter_pt ngx_http_next_header_filter;
ngx_http_output_body_filter_pt ngx_http_next_output_body_filter;

static bool is_datadog_tracing_enabled(
    const ngx_http_request_t *request,
    const ngx_http_core_loc_conf_t *core_loc_conf,
    const datadog_loc_conf_t *loc_conf) noexcept {
  // Check if this is a main request instead of a subrequest.
  if (request == request->main) {
    return loc_conf->enable_tracing;
  } else {
    // Only trace subrequests if `log_subrequest` is enabled; otherwise the
    // spans won't be finished.
    return loc_conf->enable_tracing && core_loc_conf->log_subrequest;
  }
}

ngx_int_t on_enter_block(ngx_http_request_t *request) noexcept try {
  auto core_loc_conf = static_cast<ngx_http_core_loc_conf_t *>(
      ngx_http_get_module_loc_conf(request, ngx_http_core_module));
  auto loc_conf = static_cast<datadog_loc_conf_t *>(
      ngx_http_get_module_loc_conf(request, ngx_http_datadog_module));
#ifdef WITH_RUM
  if (!is_datadog_tracing_enabled(request, core_loc_conf, loc_conf) &&
      !loc_conf->rum_enable)
    return NGX_DECLINED;
#else
  if (!is_datadog_tracing_enabled(request, core_loc_conf, loc_conf))
    return NGX_DECLINED;
#endif

  auto context = get_datadog_context(request);
  if (context == nullptr) {
    context = new DatadogContext{request, core_loc_conf, loc_conf};
    set_datadog_context(request, context);
  } else {
    try {
      context->on_change_block(request, core_loc_conf, loc_conf);
    } catch (const std::exception &e) {
      telemetry::report_error_log(e.what(), CURRENT_FRAME(request));
      // The DatadogContext may be broken, destroy it so that we don't
      // attempt to continue tracing.
      destroy_datadog_context(request);
      throw;
    }
  }
  return NGX_DECLINED;
} catch (const std::exception &e) {
  telemetry::report_error_log(e.what(), CURRENT_FRAME(request));
  ngx_log_error(NGX_LOG_ERR, request->connection->log, 0,
                "Datadog instrumentation failed for request %p: %s", request,
                e.what());
  return NGX_DECLINED;
}

#ifdef WITH_WAF
ngx_int_t on_access(ngx_http_request_t *request) noexcept try {
  if (request->main != request) {
    return NGX_DECLINED;
  }

  auto context = get_datadog_context(request);
  if (context == nullptr) {
    return NGX_DECLINED;
  }
  bool suspend = context->on_main_req_access(request);
  if (suspend) {
    return NGX_AGAIN;
  }
  return NGX_DECLINED;
} catch (const std::exception &e) {
  telemetry::report_error_log(e.what(), CURRENT_FRAME(request));
  ngx_log_error(NGX_LOG_ERR, request->connection->log, 0,
                "Datadog instrumentation failed for request %p: %s", request,
                e.what());
  return NGX_DECLINED;
}
#endif

ngx_int_t on_log_request(ngx_http_request_t *request) noexcept {
  auto context = get_datadog_context(request);
  if (context == nullptr) return NGX_DECLINED;
  try {
    context->on_log_request(request);
  } catch (const std::exception &e) {
    telemetry::report_error_log(e.what(), CURRENT_FRAME(request));
    ngx_log_error(NGX_LOG_ERR, request->connection->log, 0,
                  "Datadog instrumentation failed for request %p: %s", request,
                  e.what());
  }
  return NGX_DECLINED;
}

ngx_int_t on_header_filter(ngx_http_request_t *request) noexcept {
  DatadogContext *context = get_datadog_context(request);
  if (!context) {
    return ngx_http_next_header_filter(request);
  }

  try {
    return context->on_header_filter(request);
  } catch (const std::exception &e) {
    telemetry::report_error_log(e.what(), CURRENT_FRAME(request));
    ngx_log_error(NGX_LOG_ERR, request->connection->log, 0,
                  "Datadog instrumentation failed for request %p: %s", request,
                  e.what());
    return NGX_ERROR;
  }
}

#ifdef WITH_WAF
ngx_http_request_body_filter_pt ngx_http_next_request_body_filter;

ngx_int_t request_body_filter(ngx_http_request_t *request,
                              ngx_chain_t *chain) noexcept {
  if (request != request->main) {
    return ngx_http_next_request_body_filter(request, chain);
  }

  DatadogContext *context = get_datadog_context(request);
  if (!context) {
    return ngx_http_next_request_body_filter(request, chain);
  }

  try {
    return context->request_body_filter(request, chain);
  } catch (const std::exception &e) {
    telemetry::report_error_log(e.what(), CURRENT_FRAME(request));
    ngx_log_error(NGX_LOG_ERR, request->connection->log, 0,
                  "Datadog instrumentation failed in request body filter for "
                  "request %p: %s",
                  request, e.what());
    return NGX_ERROR;
  }
}
#endif

ngx_int_t on_output_body_filter(ngx_http_request_t *request,
                                ngx_chain_t *chain) noexcept {
  if (request != request->main) {
    return ngx_http_next_output_body_filter(request, chain);
  }

  DatadogContext *context = get_datadog_context(request);
  if (!context) {
    return ngx_http_next_output_body_filter(request, chain);
  }

  try {
    return context->on_output_body_filter(request, chain);
  } catch (const std::exception &e) {
    telemetry::report_error_log(e.what(), CURRENT_FRAME(request));
    ngx_log_error(NGX_LOG_ERR, request->connection->log, 0,
                  "Datadog instrumentation failed for request %p: %s", request,
                  e.what());
    return NGX_ERROR;
  }
}

}  // namespace nginx
}  // namespace datadog
