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

const std::string integration_name = "integration_name:nginx";
const std::string integration_version = std::format(
    "integration_version:{}", std::string(datadog_semver_nginx_mod));
const std::string injector_version = "injector_version:0.1.0";

}  // namespace

namespace injection_skip {

std::shared_ptr<CounterMetric> already_injected(new CounterMetric{
    "injection.skipped",
    "rum",
    {integration_name,
     integration_version,
     injector_version,
     {"reason:already_injected"}},
    true});

std::shared_ptr<CounterMetric> invalid_content_type(new CounterMetric{
    "injection.skipped",
    "rum",
    {integration_name,
     integration_version,
     injector_version,
     {"reason:content-type"}},
    true});

std::shared_ptr<CounterMetric> no_content(new CounterMetric{
    "injection.skipped",
    "rum",
    {
        integration_name,
        integration_version,
        injector_version,
        {"reason:empty_response"},
    },
    true});

std::shared_ptr<CounterMetric> compressed_html(new CounterMetric{
    "injection.skipped",
    "rum",
    {
        integration_name,
        integration_version,
        injector_version,
        {"reason:compressed_html"},
    },
    true});

}  // namespace injection_skip

std::shared_ptr<CounterMetric> injection_succeed(new CounterMetric{
    "injection.succeed",
    "rum",
    {integration_name, integration_version, injector_version},
    true});

std::shared_ptr<CounterMetric> injection_failed(new CounterMetric{
    "injection.failed",
    "rum",
    {integration_name, integration_version, injector_version,
     "reason:missing_header_tag"},
    true});

std::shared_ptr<CounterMetric> configuration_succeed(new CounterMetric{
    "injection.initialization.succeed",
    "rum",
    {integration_name, integration_version, injector_version},
    true});

std::shared_ptr<CounterMetric> configuration_failed_invalid_json(
    new CounterMetric{"injection.initialization.failed",
                      "rum",
                      {integration_name, integration_version, injector_version,
                       "reason:invalid_json"},
                      true});

std::shared_ptr<CounterMetric> content_security_policy(new CounterMetric{
    "injection.content_security_policy",
    "rum",
    {integration_name, integration_version, injector_version, "status:seen",
     "kind:header"},
    true});

}  // namespace telemetry
}  // namespace rum
}  // namespace nginx
}  // namespace datadog
