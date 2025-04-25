#include <datadog/telemetry/metrics.h>

namespace datadog {
namespace nginx {
namespace rum {
namespace telemetry {

const std::vector<std::string>& get_common_tags();

void increment_counter(
    const datadog::telemetry::Counter& counter,
    std::initializer_list<std::string> specific_tags);

extern datadog::telemetry::Counter injection_skipped;
extern datadog::telemetry::Counter injection_succeed;
extern datadog::telemetry::Counter injection_failed;
extern datadog::telemetry::Counter content_security_policy;

}  // namespace telemetry
}  // namespace rum
}  // namespace nginx
}  // namespace datadog
