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

class OwnedDdwafHandle;
class FinalizedConfigSettings;

struct HashedStringView {
  std::string_view str;
  ngx_uint_t hash;
};

class Library {
 public:
  static std::optional<ddwaf_owned_map> initialize_security_library(
      const datadog_main_conf_t &conf);

  // returns the handle if active, otherwise an empty shared_ptr
  static std::shared_ptr<OwnedDdwafHandle> get_handle();

  // returns the handle unconditionally. It can still be an empty shared_ptr
  static std::shared_ptr<OwnedDdwafHandle> get_handle_uncond();

  static void set_active(bool value) noexcept;
  static bool active() noexcept;

  static std::optional<HashedStringView> custom_ip_header();
  static std::uint64_t waf_timeout();

  static std::vector<std::string_view> environment_variable_names();

 protected:
  static void set_handle(OwnedDdwafHandle &&handle);

  // must be handled atomically!
  static std::shared_ptr<OwnedDdwafHandle> handle_;                  // NOLINT
  static std::atomic<bool> active_;                                  // NOLINT
  static std::unique_ptr<FinalizedConfigSettings> config_settings_;  // NOLINT
};

struct DdwafHandleFreeFunctor {
  void operator()(ddwaf_handle h) {
    if (h != nullptr) {
      ddwaf_destroy(h);
    }
  }
};
class OwnedDdwafHandle
    : public FreeableResource<ddwaf_handle, DdwafHandleFreeFunctor> {
 public:
  using FreeableResource::FreeableResource;
};

}  // namespace datadog::nginx::security
