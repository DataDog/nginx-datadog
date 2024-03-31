#pragma once

#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>

extern "C" {
#include <ngx_http.h>
#include <ngx_http_request.h>
}

namespace datadog::nginx::security {

// NOLINTNEXTLINE(altera-struct-pack-align)
struct BlockSpecification {
  enum class ContentType {
    AUTO,
    HTML,
    JSON,
    NONE,
  };
  int status;
  ContentType ct;
  std::string_view location;
};

class BlockingService {
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
  static std::unique_ptr<BlockingService> instance;

 public:
  static void initialize(std::optional<std::string_view> templ_html,
                         std::optional<std::string_view> templ_json);

  static BlockingService *get_instance() { return instance.get(); }

  void block(BlockSpecification spec, ngx_http_request_t &req);

 private:
  BlockingService(std::optional<std::string_view> templ_html_path,
                  std::optional<std::string_view> templ_json_path);

  static std::string load_template(std::string_view path);

  static void push_header(ngx_http_request_t &req, std::string_view name,
                          std::string_view value);

  ngx_str_t templ_html_{};
  ngx_str_t templ_json_{};
  std::string custom_templ_html_;
  std::string custom_templ_json_;
};

}  // namespace datadog::nginx::security
