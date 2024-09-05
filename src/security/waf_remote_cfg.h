#pragma once

#include <datadog/datadog_agent_config.h>
#include <datadog/tracer_config.h>

#include <memory>
#include <optional>

#include "../ngx_logger.h"
#include "ddwaf_obj.h"

namespace datadog::nginx::security {
void register_default_config(ddwaf_owned_map default_config,
                             std::shared_ptr<datadog::nginx::NgxLogger> logger);
void register_with_remote_cfg(datadog::tracing::DatadogAgentConfig &tc,
                              bool accept_cfg_update,
                              bool subscribe_activation);
}  // namespace datadog::nginx::security
