#pragma once

#include <opentracing/tracer.h>

#include <chrono>
#include <memory>
#include <vector>

#include "datadog_conf.h"
#include "propagation_header_querier.h"
#include "request_tracing.h"
#include "string_view.h"

extern "C" {
#include <nginx.h>
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
}

namespace datadog {
namespace nginx {

class DatadogContext {
 public:
  DatadogContext(ngx_http_request_t* request, ngx_http_core_loc_conf_t* core_loc_conf,
                 datadog_loc_conf_t* loc_conf);

  void on_change_block(ngx_http_request_t* request, ngx_http_core_loc_conf_t* core_loc_conf,
                       datadog_loc_conf_t* loc_conf);

  void on_log_request(ngx_http_request_t* request);

  ngx_str_t lookup_propagation_header_variable_value(ngx_http_request_t* request, string_view key);

  ngx_str_t lookup_span_variable_value(ngx_http_request_t* request, string_view key);

 private:
  std::vector<RequestTracing> traces_;

  RequestTracing* find_trace(ngx_http_request_t* request);

  const RequestTracing* find_trace(ngx_http_request_t* request) const;
};

DatadogContext* get_datadog_context(ngx_http_request_t* request) noexcept;

void set_datadog_context(ngx_http_request_t* request, DatadogContext* context);

void destroy_datadog_context(ngx_http_request_t* request) noexcept;
}  // namespace nginx
}  // namespace datadog
