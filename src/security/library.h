#pragma once

#include <ddwaf.h>

#include <atomic>
#include <memory>
#include <string>
#include <string_view>

#include "../datadog_conf.h"
#include "ddwaf_obj.h"

namespace datadog::nginx::security {

class waf_handle;
class FinalizedConfigSettings;

struct hashed_string_view {
  std::string_view str;
  ngx_uint_t hash;
};

class library {
 public:
  static std::optional<ddwaf_owned_map> initialize_security_library(
      const datadog_main_conf_t &conf);

  static bool update_ruleset(const ddwaf_map_obj &spec,
                             std::optional<ddwaf_arr_obj> new_merged_actions);

  // returns the handle if active, otherwise an empty shared_ptr
  static std::shared_ptr<waf_handle> get_handle();

  // returns the handle unconditionally. It can still be an empty shared_ptr
  static std::shared_ptr<waf_handle> get_handle_uncond();

  static void set_active(bool value) noexcept;
  static bool active() noexcept;

  static std::optional<hashed_string_view> custom_ip_header();
  static std::uint64_t waf_timeout();

  static std::vector<std::string_view> environment_variable_names();

 protected:
  static void set_handle(std::shared_ptr<waf_handle> handle);

  static std::shared_ptr<waf_handle> handle_;  // NOLINT
  static std::atomic<bool> active_;
  static std::unique_ptr<FinalizedConfigSettings> config_settings_;
};

struct action_info {
  using str_or_int = std::variant<std::string, int>;

  std::string type;
  std::map<std::string, str_or_int, std::less<>> parameters;
};

struct ddwaf_handle_free_functor {
  void operator()(ddwaf_handle h) {
    if (h != nullptr) {
      ddwaf_destroy(h);
    }
  }
};
struct owned_ddwaf_handle
    : freeable_resource<ddwaf_handle, ddwaf_handle_free_functor> {
  using freeable_resource::freeable_resource;
};

class waf_handle {
 public:
  using action_info_map_t =
      std::map<std::string /* id */, action_info, std::less<>>;

  waf_handle() = default;
  waf_handle(owned_ddwaf_handle h, const ddwaf_arr_obj &merged_actions);
  waf_handle(owned_ddwaf_handle h, action_info_map_t actions)
      : handle_{std::move(h)}, action_info_map_{std::move(actions)} {}

  waf_handle(const waf_handle &) = delete;
  waf_handle &operator=(const waf_handle &) = delete;
  waf_handle(waf_handle &&) = delete;
  waf_handle &operator=(waf_handle &&) = delete;

  ddwaf_handle get() const { return handle_.resource; }

  const action_info_map_t &action_info_map() const { return action_info_map_; }

 private:
  static action_info_map_t extract_actions(const ddwaf_arr_obj &actions);

  owned_ddwaf_handle handle_{nullptr};
  action_info_map_t action_info_map_;

  static action_info_map_t default_actions();
};

}  // namespace datadog::nginx::security
