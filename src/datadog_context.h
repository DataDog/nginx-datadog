#pragma once

#include "datadog_conf.h"
#include "ot.h"

#include "request_tracing.h"
#include "span_context_querier.h"

#include <opentracing/tracer.h>
#include <chrono>
#include <memory>
#include <vector>

extern "C" {
#include <nginx.h>
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
}

namespace datadog {
namespace nginx {
//------------------------------------------------------------------------------
// DatadogContext
//------------------------------------------------------------------------------
class DatadogContext {
 public:
  DatadogContext(ngx_http_request_t* request,
                     ngx_http_core_loc_conf_t* core_loc_conf,
                     datadog_loc_conf_t* loc_conf);

  void on_change_block(ngx_http_request_t* request,
                       ngx_http_core_loc_conf_t* core_loc_conf,
                       datadog_loc_conf_t* loc_conf);

  void on_log_request(ngx_http_request_t* request);

  ngx_str_t lookup_span_context_value(ngx_http_request_t* request,
                                      ot::string_view key);

  ngx_str_t get_binary_context(ngx_http_request_t* request) const;

 private:
  std::vector<RequestTracing> traces_;

  RequestTracing* find_trace(ngx_http_request_t* request);

  const RequestTracing* find_trace(ngx_http_request_t* request) const;
};

//------------------------------------------------------------------------------
// get_datadog_context
//------------------------------------------------------------------------------
DatadogContext* get_datadog_context(
    ngx_http_request_t* request) noexcept;

//------------------------------------------------------------------------------
// set_datadog_context
//------------------------------------------------------------------------------
void set_datadog_context(ngx_http_request_t* request,
                             DatadogContext* context);

//------------------------------------------------------------------------------
// destroy_datadog_context
//------------------------------------------------------------------------------
void destroy_datadog_context(ngx_http_request_t* request) noexcept;
}  // namespace nginx
}  // namespace datadog
