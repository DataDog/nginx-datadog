#pragma once

#include <ddwaf.h>

#include <memory>
#include <string_view>
#include <vector>

namespace datadog {
namespace nginx {
namespace security {

class waf_handle {
 public:
  waf_handle() = default;
  explicit waf_handle(ddwaf_object *ruleset);
  ~waf_handle();

  const ddwaf_handle get() const { return handle_; }

 protected:
  ddwaf_handle handle_{nullptr};
};

class library {
 public:
  static void initialise_security_library(std::string_view ruleset);
  static std::shared_ptr<waf_handle> get_handle() { return handle_; }
  static std::vector<std::string_view> environment_variable_names();

 protected:
  static std::shared_ptr<waf_handle> handle_;
};

}  // namespace security
}  // namespace nginx
}  // namespace datadog
