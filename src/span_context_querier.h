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
// TODO: Let the naming of relevant span properties
// be independent of details of injection.
// Using injected keys was clever for generality,
// but now we have a specific Span implementation,
// so let it be $datadog_trace_id instead of $datadog_context_x_datadog_trace_id.
// Also, call the span ID "span ID" instead of "parent ID."
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
