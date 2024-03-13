#pragma once

#include <ddwaf.h>

#include "ddwaf_obj.h"

#include <atomic>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace datadog::nginx::security {

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
  waf_handle(ddwaf_handle h, const ddwaf_map_obj &merged_actions);

  waf_handle(const waf_handle &) = delete;
  waf_handle &operator=(const waf_handle &) = delete;
  waf_handle(waf_handle &&) = delete;
  waf_handle &operator=(waf_handle &&) = delete;
  ~waf_handle() noexcept {
    if (handle_ != nullptr) {
      ddwaf_destroy(handle_);
    }
  }

  ddwaf_handle get() const { return handle_; }

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

  static bool update_ruleset(const ddwaf_map_obj &spec);
  
  static std::shared_ptr<waf_handle> get_handle() {
    if (active_.load(std::memory_order_relaxed)) {
      return get_handle_uncond();
    }
    return {};
  }

  static std::shared_ptr<waf_handle> get_handle_uncond() {
    return std::atomic_load(&handle_);
  }

  static void set_active(bool value) noexcept {
    active_.store(value, std::memory_order_relaxed);
  }

  static std::vector<std::string_view> environment_variable_names();

 protected:
  static std::shared_ptr<waf_handle> handle_; // NOLINT
  static std::atomic<bool> active_;
};

} // namespace datadog::nginx::security 
