#pragma once

#include <ddwaf.h>

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace datadog {
namespace nginx {
namespace security {

struct action_info {
  using str_or_int = std::variant<std::string, int>;

  std::string type;
  std::map<std::string, str_or_int, std::less<>> parameters;
};

class waf_handle {
 public:
  using action_info_map_t =
      std::map<std::string /* id */, action_info, std::less<>>;

  waf_handle() = default;
  explicit waf_handle(ddwaf_object *ruleset);
  ~waf_handle();

  const ddwaf_handle get() const { return handle_; }

  const action_info_map_t &action_info_map() const { return action_info_map_; }

 private:
  static action_info_map_t extract_actions(const ddwaf_object &ruleset);

  ddwaf_handle handle_{nullptr};
  action_info_map_t action_info_map_;

  static action_info_map_t default_actions() {
    return {
        {"block",
         {"block_request",
          {{"status_code", 403}, {"type", "auto"}, {"grpc_status_code", 10}}}}};
  }
};

class library {
 public:
  static void initialise_security_library(
    std::string_view ruleset,
    std::string_view template_html,
    std::string_view template_json);
  static std::shared_ptr<waf_handle> get_handle() { return handle_; }
  static std::vector<std::string_view> environment_variable_names();

 protected:
  static std::shared_ptr<waf_handle> handle_;
};

}  // namespace security
}  // namespace nginx
}  // namespace datadog
