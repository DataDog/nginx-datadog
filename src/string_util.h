// Utility functions to make it easier to interoperate with C++.

#pragma once

#include <algorithm>
#include <cctype>
#include <string>

#include <string_view>

extern "C" {
#include <ngx_core.h>
}

namespace datadog {
namespace nginx {

inline std::string to_string(const ngx_str_t& ngx_str) {
  return {reinterpret_cast<char*>(ngx_str.data), ngx_str.len};
}

inline std::string_view to_string_view(const ngx_str_t& s) {
  return {reinterpret_cast<char*>(s.data), s.len};
}

inline std::string_view str(const ngx_str_t& s) { return to_string_view(s); }

ngx_str_t to_ngx_str(ngx_pool_t* pool, std::string_view s);

inline ngx_str_t to_ngx_str(std::string_view s) {
  ngx_str_t result;
  result.len = s.size();
  result.data = reinterpret_cast<unsigned char*>(const_cast<char*>(s.data()));
  return result;
}

inline char to_upper(unsigned char c) { return static_cast<char>(std::toupper(c)); }

inline char to_lower(unsigned char c) { return static_cast<char>(std::tolower(c)); }

inline char hyphen_to_underscore(char c) {
  if (c == '-') return '_';
  return c;
}

// Perform the transformations on header characters described by
// http://nginx.org/en/docs/http/ngx_http_core_module.html#var_http_
inline char header_transform_char(char c) { return to_lower(hyphen_to_underscore(c)); }

inline bool starts_with(const std::string_view& subject, const std::string_view& prefix) {
  if (prefix.size() > subject.size()) {
    return false;
  }

  return std::mismatch(subject.begin(), subject.end(), prefix.begin()).second == prefix.end();
}

inline std::string_view slice(const std::string_view& text, int begin, int end) {
  if (begin < 0) {
    begin += text.size();
  }
  if (end < 0) {
    end += text.size();
  }
  return std::string_view(text.data() + begin, end - begin);
}

inline std::string_view slice(const std::string_view& text, int begin) {
  return slice(text, begin, text.size());
}

}  // namespace nginx
}  // namespace datadog
