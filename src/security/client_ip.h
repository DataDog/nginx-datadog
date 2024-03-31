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
  struct HashedStringView {
    std::string_view sv;
    ngx_uint_t hash;
  };

  ClientIp(std::optional<HashedStringView> configured_header,
           const ngx_http_request_t &request);

  std::optional<std::string> resolve() const;

 private:
  std::optional<HashedStringView> configured_header_;  // lc
  const ngx_http_request_t &request_;
};
}  // namespace datadog::nginx::security
