#pragma once

#include "datadog_conf.h"
#include "string_view.h"

#include "span_context_querier.h"

#include <opentracing/tracer.h>
#include <chrono>
#include <memory>

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
  RequestTracing(ngx_http_request_t *request,
                 ngx_http_core_loc_conf_t *core_loc_conf,
                 datadog_loc_conf_t *loc_conf,
                 const ot::SpanContext *parent_span_context = nullptr);

  void on_change_block(ngx_http_core_loc_conf_t *core_loc_conf,
                       datadog_loc_conf_t *loc_conf);

  void on_log_request();

  ngx_str_t lookup_span_context_value(string_view key);

  ngx_str_t get_binary_context() const;

  const ot::SpanContext &context() const {
    return request_span_->context();
  }

  ngx_http_request_t *request() const { return request_; }

 private:
  ngx_http_request_t *request_;
  datadog_main_conf_t *main_conf_;
  ngx_http_core_loc_conf_t *core_loc_conf_;
  datadog_loc_conf_t *loc_conf_;
  SpanContextQuerier span_context_querier_;
  std::unique_ptr<ot::Span> request_span_;
  std::unique_ptr<ot::Span> span_;

  const ot::Span &active_span() const;

  void on_exit_block(std::chrono::steady_clock::time_point finish_timestamp =
                         std::chrono::steady_clock::now());
};

}  // namespace nginx
}  // namespace datadog
