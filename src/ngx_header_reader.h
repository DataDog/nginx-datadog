#pragma once

#include <datadog/dict_reader.h>

#include <algorithm>
#include <cctype>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "array_util.h"
#include "dd.h"

extern "C" {
#include <nginx.h>
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
}

namespace datadog {
namespace nginx {

class NgxHeaderReader : public dd::DictReader {
  std::unordered_map<std::string_view, std::string_view> headers_;
  mutable std::string buffer_;

 public:
  explicit NgxHeaderReader(const ngx_list_t* headers) {
    for_each<ngx_table_elt_t>(*headers, [&](const ngx_table_elt_t& header) {
      auto key = std::string_view{reinterpret_cast<char*>(header.lowcase_key),
                                  header.key.len};
      auto value = std::string_view{reinterpret_cast<char*>(header.value.data),
                                    header.value.len};
      headers_.emplace(key, value);
    });
  }

  std::optional<std::string_view> lookup(std::string_view key) const override {
    buffer_.clear();
    std::transform(key.begin(), key.end(), std::back_inserter(buffer_),
                   [](unsigned char ch) { return std::tolower(ch); });
    const auto found = headers_.find(buffer_);
    if (found != headers_.end()) {
      return found->second;
    }
    return std::nullopt;
  }

  void visit(
      const std::function<void(std::string_view key, std::string_view value)>&
          visitor) const override {
    for (const auto& [key, value] : headers_) {
      visitor(key, value);
    }
  }
};

}  // namespace nginx
}  // namespace datadog
