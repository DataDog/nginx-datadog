#pragma once

#include <chrono>
#include <memory>
#include <string_view>
#include <vector>

#include "datadog_conf.h"
#include "request_tracing.h"
#ifdef WITH_WAF
#include "security/context.h"
#endif

#ifdef WITH_RUM
#include "rum/injection.h"
#endif

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
  DatadogContext(ngx_http_request_t* request,
                 ngx_http_core_loc_conf_t* core_loc_conf,
                 datadog_loc_conf_t* loc_conf);

  void on_change_block(ngx_http_request_t* request,
                       ngx_http_core_loc_conf_t* core_loc_conf,
                       datadog_loc_conf_t* loc_conf);

#ifdef WITH_WAF
  bool on_main_req_access(ngx_http_request_t* request);
#endif

  ngx_int_t on_header_filter(ngx_http_request_t* request);

#ifdef WITH_WAF
  ngx_int_t request_body_filter(ngx_http_request_t* request,
                                ngx_chain_t* chain);
#endif

  ngx_int_t on_output_body_filter(ngx_http_request_t* request,
                                  ngx_chain_t* chain);

  void on_log_request(ngx_http_request_t* request);

  ngx_int_t on_precontent_phase(ngx_http_request_t* request);

  ngx_str_t lookup_span_variable_value(ngx_http_request_t* request,
                                       std::string_view key);

  RequestTracing& single_trace();

#ifdef WITH_WAF
  security::Context* get_security_context() { return sec_ctx_.get(); }
#endif

 private:
  std::vector<RequestTracing> traces_;
#ifdef WITH_WAF
  std::unique_ptr<security::Context> sec_ctx_;
#endif

#ifdef WITH_RUM
  rum::InjectionHandler rum_ctx_;
#endif

  RequestTracing* find_trace(ngx_http_request_t* request);

  const RequestTracing* find_trace(ngx_http_request_t* request) const;
};

DatadogContext* get_datadog_context(ngx_http_request_t* request) noexcept;

void set_datadog_context(ngx_http_request_t* request, DatadogContext* context);

void destroy_datadog_context(ngx_http_request_t* request) noexcept;
}  // namespace nginx
}  // namespace datadog
