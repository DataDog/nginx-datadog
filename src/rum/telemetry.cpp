#include <datadog/telemetry/telemetry.h>

#include "telemetry.h"

#include <format>
#include <string_view>

#include "version.h"

using namespace datadog::telemetry;

namespace datadog {
namespace nginx {
namespace rum {
namespace telemetry {

namespace {
const std::vector<std::string>& initialize_common_tags() {
    static const std::vector<std::string> common_tags = {
        "integration_name:nginx",
        std::format("integration_version:{}", std::string(datadog_semver_nginx_mod)),
        "injector_version:0.1.0"
    };
    return common_tags;
}
}

const std::vector<std::string>& get_common_tags() {
    return initialize_common_tags();
}

void increment_counter(
    const datadog::telemetry::Counter& counter,
    const std::vector<std::string>& specific_tags)
{
    const auto& common = get_common_tags();
    std::vector<std::string> all_tags;
    all_tags.reserve(common.size() + specific_tags.size());
    all_tags.insert(all_tags.end(), common.begin(), common.end());
    all_tags.insert(all_tags.end(), specific_tags.begin(), specific_tags.end());
    datadog::telemetry::counter::increment(counter, all_tags);
}

void increment_counter(
    const datadog::telemetry::Counter& counter,
    std::initializer_list<std::string> specific_tags)
{
    std::vector<std::string> specific_tags_vec(specific_tags.begin(), specific_tags.end());
    increment_counter(counter, specific_tags_vec);
}

datadog::telemetry::Counter injection_skipped = {
    "injection.skipped",
    "rum",
    true
};

datadog::telemetry::Counter injection_succeed = {
    "injection.succeed",
    "rum",
    true
};

datadog::telemetry::Counter injection_failed = {
    "injection.failed",
    "rum",
    true
};

datadog::telemetry::Counter content_security_policy = {
    "injection.content_security_policy",
    "rum",
    true
};

}  // namespace telemetry
}  // namespace rum
}  // namespace nginx
}  // namespace datadog
