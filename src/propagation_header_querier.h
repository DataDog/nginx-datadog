#pragma once

#include <datadog/span.h>

#include <string>
#include <string_view>
#include <utility>
#include <unordered_map>

#include "datadog_conf.h"
#include "dd.h"

extern "C" {
#include <nginx.h>
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
}

namespace datadog {
namespace nginx {

class PropagationHeaderQuerier {
 public:
  PropagationHeaderQuerier() noexcept {}

  ngx_str_t lookup_value(ngx_http_request_t* request, const dd::Span& span, std::string_view key);

 private:
  const dd::Span* values_span_ = nullptr;

  std::unordered_map<std::string, std::string> span_context_expansion_;
  std::string buffer_;

  void expand_values(ngx_http_request_t* request, const dd::Span& span);
};

}  // namespace nginx
}  // namespace datadog
