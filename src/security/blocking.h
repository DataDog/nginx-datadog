#pragma once

#include <memory>
#include <optional>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string_view>

extern "C" {
#include <ngx_http.h>
#include <ngx_http_request.h>
}

namespace datadog::nginx::security {

// NOLINTNEXTLINE(altera-struct-pack-align)
struct block_spec {
  enum class ct {
    AUTO,
    HTML,
    JSON,
    NONE,
  };
  int status;
  ct ct;
  std::string_view location;
};

class blocking_service {
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
  static std::unique_ptr<blocking_service> instance;

 public:
  static void initialize(std::optional<std::string_view> templ_html,
                         std::optional<std::string_view> templ_json);

  static blocking_service *get_instance() { return instance.get(); }

  void block(block_spec spec, ngx_http_request_t &req);

 private:
  blocking_service(std::optional<std::string_view> templ_html_path,
                   std::optional<std::string_view> templ_json_path);

  static std::string load_template(std::string_view path);

  static void push_header(ngx_http_request_t &req, std::string_view name,
                          std::string_view value);

  ngx_str_t templ_html{};
  ngx_str_t templ_json{};
  std::string custom_templ_html;
  std::string custom_templ_json;
};

} // namespace datadog::nginx::security
