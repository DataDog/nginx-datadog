#include <datadog/telemetry/metrics.h>

#include <format>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "version.h"

namespace datadog {
namespace nginx {
namespace rum {
namespace telemetry {

template <typename... T>
auto build_tags(T&&... specific_tags) {
  std::vector<std::string> tags{std::forward<T>(specific_tags)...};
  tags.emplace_back("integration_name:nginx");
  tags.emplace_back("injector_version:0.1.0");
  tags.emplace_back(std::format("integration_version:{}-rum_{}",
                                std::string(datadog_semver_nginx_mod),
                                "e9bb286"));

  return tags;
}

const extern datadog::telemetry::Counter injection_skipped;
const extern datadog::telemetry::Counter injection_succeed;
const extern datadog::telemetry::Counter injection_failed;
const extern datadog::telemetry::Counter content_security_policy;

}  // namespace telemetry
}  // namespace rum
}  // namespace nginx
}  // namespace datadog
