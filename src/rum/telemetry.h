#include <datadog/telemetry/metrics.h>

#include <format>
#include <string>
#include <utility>
#include <vector>

#include "version.h"

namespace datadog {
namespace nginx {
namespace rum {
namespace telemetry {

template <typename... T>
auto build_telemetry_tags(T&&... specific_tags) {
  static const std::vector<std::string> common_tags = {
      "integration_name:nginx",
      std::format("integration_version:{}",
                  std::string(datadog_semver_nginx_mod)),
      "injector_version:0.1.0"};

  std::vector<std::string> tags{std::forward<T>(specific_tags)...};

  tags.reserve(tags.size() + common_tags.size());
  tags.insert(tags.end(), common_tags.begin(), common_tags.end());

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
