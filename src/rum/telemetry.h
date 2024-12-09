#include <datadog/telemetry/metrics.h>

namespace datadog {
namespace nginx {
namespace rum {
namespace telemetry {

namespace injection_skip {
extern std::shared_ptr<datadog::telemetry::CounterMetric> already_injected;
extern std::shared_ptr<datadog::telemetry::CounterMetric> invalid_content_type;
extern std::shared_ptr<datadog::telemetry::CounterMetric> no_content;
extern std::shared_ptr<datadog::telemetry::CounterMetric> compressed_html;

}  // namespace injection_skip

extern std::shared_ptr<datadog::telemetry::CounterMetric> injection_succeed;
extern std::shared_ptr<datadog::telemetry::CounterMetric> injection_failed;
extern std::shared_ptr<datadog::telemetry::CounterMetric> configuration_succeed;
extern std::shared_ptr<datadog::telemetry::CounterMetric>
    configuration_failed_invalid_json;
extern std::shared_ptr<datadog::telemetry::CounterMetric>
    content_security_policy;

}  // namespace telemetry
}  // namespace rum
}  // namespace nginx
}  // namespace datadog
