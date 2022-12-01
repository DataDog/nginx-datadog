#pragma once

extern "C" {
#include <ngx_config.h>
#include <ngx_http.h>
}
#include <datadog/span.h>
#include <optional>
#include <string>
#include <string_view>

namespace datadog::nginx::security {
class ClientIp {
 public:
 // XXX: use and move to library
  static std::string normalize_configured_header(std::string_view config);

  struct hashed_string_view {
    std::string_view sv;
    ngx_uint_t hash;
  };

  ClientIp(std::optional<hashed_string_view> configured_header,
           const ngx_http_request_t &request);

  std::optional<std::string> resolve() const;

 private:
  std::optional<hashed_string_view> configured_header_;  // lc
  const ngx_http_request_t &request_;
};
}  // namespace datadog::nginx::security
