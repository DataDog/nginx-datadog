#include "telemetry.h"

#include <format>
#include <string_view>

#include "version.h"

namespace datadog {
namespace nginx {
namespace rum {
namespace telemetry {

namespace {

const std::string integration_name = "integration_name:nginx";
const std::string integration_version = "integration_version:1.3.0";
const std::string injector_version = "injector_version:0.1.0";

}  // namespace

namespace injection_skip {

tracing::CounterMetric already_injected{"injection.skipped",
                                        {integration_name,
                                         integration_version,
                                         injector_version,
                                         {"reason:already_injected"}},
                                        true,
                                        "rum"};

tracing::CounterMetric invalid_content_type{"injection.skipped",
                                            {integration_name,
                                             integration_version,
                                             injector_version,
                                             {"reason:content-type"}},
                                            true,
                                            "rum"};
tracing::CounterMetric no_content{"injection.skipped",
                                  {
                                      integration_name,
                                      integration_version,
                                      injector_version,
                                      {"reason:empty_response"},
                                  },
                                  true,
                                  "rum"};
tracing::CounterMetric compressed_html{"injection.skipped",
                                       {
                                           integration_name,
                                           integration_version,
                                           injector_version,
                                           {"reason:compressed_html"},
                                       },
                                       true,
                                       "rum"};

}  // namespace injection_skip

tracing::CounterMetric injection_succeed{
    "injection.succeed",
    {integration_name, integration_version, injector_version},
    true,
    "rum"};

tracing::CounterMetric injection_failed{
    "injection.failed",
    {integration_name, integration_version, injector_version,
     "reason:missing_header_tag"},
    true,
    "rum"};

tracing::CounterMetric configuration_succeed{
    "injection.initialization.succeed",
    {integration_name, integration_version, injector_version},
    true,
    "rum"};

tracing::CounterMetric configuration_failed_invalid_json{
    "injection.initialization.failed",
    {integration_name, integration_version, injector_version,
     "reason:invalid_json"},
    true,
    "rum"};

tracing::HistogramMetric injection_duration{"injection.ms",
                                            {
                                                integration_name,
                                                integration_version,
                                                injector_version,
                                            },
                                            true,
                                            "rum"};

tracing::HistogramMetric response_size{"injection.response.bytes",
                                       {
                                           integration_name,
                                           integration_version,
                                           injector_version,
                                       },
                                       true,
                                       "rum"};

}  // namespace telemetry
}  // namespace rum
}  // namespace nginx
}  // namespace datadog
