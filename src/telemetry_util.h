#pragma once

#include <format>
#include <string_view>

#include "string_util.h"

extern "C" {
#include <ngx_http.h>
}

namespace datadog::nginx {

constexpr std::string_view relative_filepath(
    std::string_view absolute_filepath) {
  constexpr std::string_view prefix = "nginx-datadog";
  size_t pos = absolute_filepath.find(prefix);
  return pos != std::string_view::npos ? absolute_filepath.substr(pos)
                                       : absolute_filepath;
}

inline std::string make_current_frame(ngx_http_request_t *request,
                                      std::string_view file, size_t line,
                                      std::string_view function) {
  // clang-format off
  return std::format("Exception catched:\n"
                     "   at {} ({}:{})\n"
                     "   at {}", function, file, line,
                                to_string_view(request->uri));
  // clang-format on
}

}  // namespace datadog::nginx

#define CURRENT_FRAME(request)                                        \
  datadog::nginx::make_current_frame(                                 \
      request, datadog::nginx::relative_filepath(__FILE__), __LINE__, \
      __FUNCTION__)
