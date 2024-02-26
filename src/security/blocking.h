#pragma once

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string_view>

extern "C" {
#include <ngx_http.h>
#include <ngx_http_request.h>
}

namespace datadog {
namespace nginx {
namespace security {

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
  static blocking_service *instance;

 public:
  static void initialize(std::string_view templ_html,
                         std::string_view templ_json);

  static blocking_service *get_instance() { return instance; }

  void block(block_spec spec, ngx_http_request_t &req);

 private:
  blocking_service(std::string_view templ_html_path,
                   std::string_view templ_json_path);

  static std::string load_template(std::string_view path);

  static void push_header(ngx_http_request_t &req, std::string_view name,
                          std::string_view value);

  ngx_str_t templ_html;
  ngx_str_t templ_json;
  std::string custom_templ_html;
  std::string custom_templ_json;
};

}  // namespace security
}  // namespace nginx
}  // namespace datadog
