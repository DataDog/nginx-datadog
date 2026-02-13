/*
 * Unless explicitly stated otherwise all files in this repository are licensed
 * under the Apache 2.0 License. This product includes software developed at
 * Datadog (https://www.datadoghq.com/).
 *
 * Copyright 2024-Present Datadog, Inc.
 */

#pragma once

#include <datadog/telemetry/metrics.h>
#include <memory>

namespace datadog::rum::telemetry {

namespace injection_skip {
extern std::shared_ptr<datadog::telemetry::CounterMetric> already_injected;
extern std::shared_ptr<datadog::telemetry::CounterMetric> invalid_content_type;
extern std::shared_ptr<datadog::telemetry::CounterMetric> no_content;
extern std::shared_ptr<datadog::telemetry::CounterMetric> compressed_html;
} // namespace injection_skip

extern std::shared_ptr<datadog::telemetry::CounterMetric> injection_succeed;
extern std::shared_ptr<datadog::telemetry::CounterMetric> injection_failed;
extern std::shared_ptr<datadog::telemetry::CounterMetric> configuration_succeed;
extern std::shared_ptr<datadog::telemetry::CounterMetric>
    configuration_failed_invalid_json;

/*extern tracing::HistogramMetric injection_duration;*/
/*extern tracing::HistogramMetric response_size;*/

} // namespace datadog::rum::telemetry
