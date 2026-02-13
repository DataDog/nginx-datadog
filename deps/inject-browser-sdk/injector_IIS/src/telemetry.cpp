// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache 2.0 License. This product includes software developed at
// Datadog (https://www.datadoghq.com/).
//
// Copyright 2024-Present Datadog, Inc.

#include "telemetry.h"

using namespace datadog::telemetry;

namespace datadog::rum::telemetry {
namespace {

const std::string integration_name = "integration_name:iis";
const std::string integration_version = "integration_version:1.0.1";
const std::string injector_version = "injector_version:0.1.0";

} // namespace
namespace injection_skip {

std::shared_ptr<CounterMetric>
    already_injected(new CounterMetric("injection.skipped", "rum",
                                       {integration_name,
                                        integration_version,
                                        injector_version,
                                        {"reason:already_injected"}},
                                       true));

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

} // namespace injection_skip

std::shared_ptr<CounterMetric> injection_succeed(new CounterMetric{
    "injection.succeed",
    "rum",
    {integration_name, integration_version, injector_version},
    true});

std::shared_ptr<CounterMetric> injection_failed(new CounterMetric{
    "injection.failed",
    "rum",
    {integration_name, integration_version, injector_version,
     "reason:missing_head_tag"},
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
} // namespace datadog::rum::telemetry
