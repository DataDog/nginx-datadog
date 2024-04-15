#pragma once

#include <ddwaf.h>

#include <atomic>
#include <memory>
#include <string>
#include <string_view>

#include "../datadog_conf.h"
#include "ddwaf_obj.h"

namespace datadog::nginx::security {

inline constexpr auto kConfigMaxDepth = 25;

class WafHandle;
class FinalizedConfigSettings;

struct HashedStringView {
  std::string_view str;
  ngx_uint_t hash;
};

class Library {
 public:
  static std::optional<ddwaf_owned_map> initialize_security_library(
      const datadog_main_conf_t &conf);

  static bool update_ruleset(const ddwaf_map_obj &spec,
                             std::optional<ddwaf_arr_obj> new_merged_actions);

  // returns the handle if active, otherwise an empty shared_ptr
  static std::shared_ptr<WafHandle> get_handle();

  // returns the handle unconditionally. It can still be an empty shared_ptr
  static std::shared_ptr<WafHandle> get_handle_uncond();

  static void set_active(bool value) noexcept;
  static bool active() noexcept;

  static std::optional<HashedStringView> custom_ip_header();
  static std::uint64_t waf_timeout();

  static std::vector<std::string_view> environment_variable_names();

 protected:
  static void set_handle(std::shared_ptr<WafHandle> handle);

  static std::shared_ptr<WafHandle> handle_;                         // NOLINT
  static std::atomic<bool> active_;                                  // NOLINT
  static std::unique_ptr<FinalizedConfigSettings> config_settings_;  // NOLINT
};

struct ActionInfo {
  using StrOrInt = std::variant<std::string, int>;

  std::string type;
  std::map<std::string, StrOrInt, std::less<>> parameters;
};

struct DdwafHandleFreeFunctor {
  void operator()(ddwaf_handle h) {
    if (h != nullptr) {
      ddwaf_destroy(h);
    }
  }
};
struct OwnedDdwafHandle
    : FreeableResource<ddwaf_handle, DdwafHandleFreeFunctor> {
  using FreeableResource::FreeableResource;
};

class WafHandle {
 public:
  using action_info_map_t =  // NOLINT(readability-identifier-naming)
      std::map<std::string /* id */, ActionInfo, std::less<>>;

  WafHandle() = default;
  WafHandle(OwnedDdwafHandle h, const ddwaf_arr_obj &merged_actions);
  WafHandle(OwnedDdwafHandle h, action_info_map_t actions)
      : handle_{std::move(h)}, action_info_map_{std::move(actions)} {}

  WafHandle(const WafHandle &) = delete;
  WafHandle &operator=(const WafHandle &) = delete;
  WafHandle(WafHandle &&) = delete;
  WafHandle &operator=(WafHandle &&) = delete;

  ddwaf_handle get() const { return handle_.resource; }

  const action_info_map_t &action_info_map() const { return action_info_map_; }

 private:
  static action_info_map_t extract_actions(const ddwaf_arr_obj &actions);
  static action_info_map_t default_actions();

  OwnedDdwafHandle handle_{nullptr};
  action_info_map_t action_info_map_;
};

}  // namespace datadog::nginx::security
