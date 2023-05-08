#include "datadog_context.h"

#include <algorithm>
#include <sstream>
#include <stdexcept>

#include "ngx_http_datadog_module.h"
#include "dd.h"
#include "string_util.h"
#include <string_view>

namespace datadog {
namespace nginx {
std::unique_ptr<ot::SpanContext> extract_span_context(const ot::Tracer &tracer,
                                                      const ngx_http_request_t *request);

DatadogContext::DatadogContext(ngx_http_request_t *request,
                               ngx_http_core_loc_conf_t *core_loc_conf,
                               datadog_loc_conf_t *loc_conf) {
  traces_.emplace_back(request, core_loc_conf, loc_conf);
}

void DatadogContext::on_change_block(ngx_http_request_t *request,
                                     ngx_http_core_loc_conf_t *core_loc_conf,
                                     datadog_loc_conf_t *loc_conf) {
  auto trace = find_trace(request);
  if (trace != nullptr) {
    return trace->on_change_block(core_loc_conf, loc_conf);
  }

  // This is a new subrequest so add a RequestTracing for it.
  traces_.emplace_back(request, core_loc_conf, loc_conf, &traces_[0].context());
}

void DatadogContext::on_log_request(ngx_http_request_t *request) {
  auto trace = find_trace(request);
  if (trace == nullptr) {
    throw std::runtime_error{"on_log_request failed: could not find request trace"};
  }
  trace->on_log_request();
}

ngx_str_t DatadogContext::lookup_propagation_header_variable_value(ngx_http_request_t *request,
                                                                   std::string_view key) {
  auto trace = find_trace(request);
  if (trace == nullptr) {
    throw std::runtime_error{
        "lookup_propagation_header_variable_value failed: could not find request trace"};
  }
  return trace->lookup_propagation_header_variable_value(key);
}

ngx_str_t DatadogContext::lookup_span_variable_value(ngx_http_request_t *request,
                                                     std::string_view key) {
  auto trace = find_trace(request);
  if (trace == nullptr) {
    throw std::runtime_error{"lookup_span_variable_value failed: could not find request trace"};
  }
  return trace->lookup_span_variable_value(key);
}

RequestTracing *DatadogContext::find_trace(ngx_http_request_t *request) {
  const auto found = std::find_if(traces_.begin(), traces_.end(),
                                  [=](const auto &trace) { return trace.request() == request; });
  if (found != traces_.end()) {
    return &*found;
  }
  return nullptr;
}

const RequestTracing *DatadogContext::find_trace(ngx_http_request_t *request) const {
  return const_cast<DatadogContext *>(this)->find_trace(request);
}

static void cleanup_datadog_context(void *data) noexcept {
  delete static_cast<DatadogContext *>(data);
}

static ngx_pool_cleanup_t *find_datadog_cleanup(ngx_http_request_t *request) {
  for (auto cleanup = request->pool->cleanup; cleanup; cleanup = cleanup->next) {
    if (cleanup->handler == cleanup_datadog_context) {
      return cleanup;
    }
  }
  return nullptr;
}

DatadogContext *get_datadog_context(ngx_http_request_t *request) noexcept {
  auto context =
      static_cast<DatadogContext *>(ngx_http_get_module_ctx(request, ngx_http_datadog_module));
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
    ngx_http_set_ctx(request, static_cast<void *>(context), ngx_http_datadog_module);
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
  ngx_http_set_ctx(request, static_cast<void *>(context), ngx_http_datadog_module);
}

// Supports early destruction of the DatadogContext (in case of an
// unrecoverable error).
void destroy_datadog_context(ngx_http_request_t *request) noexcept {
  auto cleanup = find_datadog_cleanup(request);
  if (cleanup == nullptr) {
    ngx_log_error(NGX_LOG_ERR, request->connection->log, 0,
                  "Unable to find Datadog cleanup handler for request %p", request);
    return;
  }
  delete static_cast<DatadogContext *>(cleanup->data);
  cleanup->data = nullptr;
  ngx_http_set_ctx(request, nullptr, ngx_http_datadog_module);
}

}  // namespace nginx
}  // namespace datadog
