#include "datadog_handler.h"

#include "ngx_http_datadog_module.h"

extern "C" {
#include <ngx_config.h>
}

#include "datadog_context.h"

extern "C" {
extern ngx_module_t ngx_http_datadog_module;
}

namespace datadog {
namespace nginx {

static bool is_datadog_enabled(const ngx_http_request_t *request,
                               const ngx_http_core_loc_conf_t *core_loc_conf,
                               const datadog_loc_conf_t *loc_conf) noexcept {
  // Check if this is a main request instead of a subrequest.
  if (request == request->main)
    return loc_conf->enable;
  else
    // Only trace subrequests if `log_subrequest` is enabled; otherwise the
    // spans won't be finished.
    return loc_conf->enable && core_loc_conf->log_subrequest;
}

ngx_int_t on_enter_block(ngx_http_request_t *request) noexcept try {
  auto core_loc_conf = static_cast<ngx_http_core_loc_conf_t *>(
      ngx_http_get_module_loc_conf(request, ngx_http_core_module));
  auto loc_conf = static_cast<datadog_loc_conf_t *>(
      ngx_http_get_module_loc_conf(request, ngx_http_datadog_module));
  if (!is_datadog_enabled(request, core_loc_conf, loc_conf))
    return NGX_DECLINED;

  auto context = get_datadog_context(request);
  if (context == nullptr) {
    context = new DatadogContext{request, core_loc_conf, loc_conf};
    set_datadog_context(request, context);
  } else {
    try {
      context->on_change_block(request, core_loc_conf, loc_conf);
    } catch (...) {
      // The DatadogContext may be broken, destroy it so that we don't
      // attempt to continue tracing.
      destroy_datadog_context(request);

      throw;
    }
  }
  return NGX_DECLINED;
} catch (const std::exception &e) {
  ngx_log_error(NGX_LOG_ERR, request->connection->log, 0,
                "Datadog instrumentation failed for request %p: %s", request,
                e.what());
  return NGX_DECLINED;
}

ngx_int_t on_log_request(ngx_http_request_t *request) noexcept {
  auto context = get_datadog_context(request);
  if (context == nullptr) return NGX_DECLINED;
  try {
    context->on_log_request(request);
  } catch (const std::exception &e) {
    ngx_log_error(NGX_LOG_ERR, request->connection->log, 0,
                  "Datadog instrumentation failed for request %p: %s", request,
                  e.what());
  }
  return NGX_DECLINED;
}

}  // namespace nginx
}  // namespace datadog
