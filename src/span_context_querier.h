#pragma once

#include "datadog_conf.h"
#include "ot.h"
#include "string_view.h"

#include <opentracing/span.h>

#include <utility>
#include <vector>

extern "C" {
#include <nginx.h>
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
}

namespace datadog {
namespace nginx {
class SpanContextQuerier {
 public:
  SpanContextQuerier() noexcept {}

  ngx_str_t lookup_value(ngx_http_request_t* request,
                         const ot::Span& span,
                         string_view key);

 private:
  const ot::Span* values_span_ = nullptr;

  std::vector<std::pair<std::string, std::string>> span_context_expansion_;

  void expand_span_context_values(ngx_http_request_t* request,
                                  const ot::Span& span);
};
}  // namespace nginx
}  // namespace datadog
