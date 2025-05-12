#include "telemetry.h"

#include <datadog/telemetry/telemetry.h>

#include <format>
#include <string_view>

#include "version.h"

using namespace datadog::telemetry;

namespace datadog {
namespace nginx {
namespace rum {
namespace telemetry {

const datadog::telemetry::Counter injection_skipped = {"injection.skipped",
                                                       "rum", true};

const datadog::telemetry::Counter injection_succeed = {"injection.succeed",
                                                       "rum", true};

const datadog::telemetry::Counter injection_failed = {"injection.failed", "rum",
                                                      true};

const datadog::telemetry::Counter content_security_policy = {
    "injection.content_security_policy", "rum", true};

}  // namespace telemetry
}  // namespace rum
}  // namespace nginx
}  // namespace datadog
