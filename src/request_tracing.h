#pragma once

#include <datadog/span.h>

#include <chrono>
#include <memory>
#include <optional>

#include "datadog_conf.h"
#include "propagation_header_querier.h"
#include <string_view>

extern "C" {
#include <nginx.h>
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
}

namespace datadog {
namespace nginx {

class RequestTracing {
 public:
  RequestTracing(ngx_http_request_t *request, ngx_http_core_loc_conf_t *core_loc_conf,
                 datadog_loc_conf_t *loc_conf,
                 std::optional<dd::Span> parent = std::nullopt);

  void on_change_block(ngx_http_core_loc_conf_t *core_loc_conf, datadog_loc_conf_t *loc_conf);

  void on_log_request();

  ngx_str_t lookup_propagation_header_variable_value(std::string_view key);
  ngx_str_t lookup_span_variable_value(std::string_view key);

  ngx_http_request_t *request() const { return request_; }

 private:
  ngx_http_request_t *request_;
  datadog_main_conf_t *main_conf_;
  ngx_http_core_loc_conf_t *core_loc_conf_;
  datadog_loc_conf_t *loc_conf_;
  PropagationHeaderQuerier propagation_header_querier_;
  std::optional<dd::Span> request_span_;
  std::optional<dd::Span> span_;

  dd::Span &active_span();

  void on_exit_block(
      std::chrono::steady_clock::time_point finish_timestamp);
};

}  // namespace nginx
}  // namespace datadog
