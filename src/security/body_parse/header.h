#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "chain_is.h"

namespace datadog::nginx::security {

struct HttpContentType {
  std::string type;
  std::string subtype;
  std::string encoding;
  std::string boundary;

  // see https://httpwg.org/specs/rfc9110.html#field.content-type
  static std::optional<HttpContentType> for_string(std::string_view sv);
};

struct MimeContentDisposition {
  std::string name;

  static std::optional<MimeContentDisposition> for_stream(
      NgxChainInputStream& is);
};

}  // namespace datadog::nginx::security
