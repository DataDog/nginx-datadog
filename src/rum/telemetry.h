#include <datadog/metrics.h>

namespace datadog {
namespace nginx {
namespace rum {
namespace telemetry {

namespace injection_skip {
extern tracing::CounterMetric already_injected;
extern tracing::CounterMetric invalid_content_type;
extern tracing::CounterMetric no_content;
extern tracing::CounterMetric compressed_html;

}  // namespace injection_skip

extern tracing::CounterMetric injection_succeed;
extern tracing::CounterMetric injection_failed;
extern tracing::CounterMetric configuration_succeed;
extern tracing::CounterMetric configuration_failed_invalid_json;

extern tracing::HistogramMetric injection_duration;
extern tracing::HistogramMetric response_size;

}  // namespace telemetry
}  // namespace rum
}  // namespace nginx
}  // namespace datadog
