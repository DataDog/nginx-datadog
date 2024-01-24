#include "tracing_library.h"

#include <datadog/dict_writer.h>
#include <datadog/environment.h>
#include <datadog/error.h>
#include <datadog/expected.h>
#include <datadog/span.h>
#include <datadog/tracer.h>
#include <datadog/tracer_config.h>

#include <algorithm>
#include <cassert>
#include <datadog/json.hpp>
#include <iterator>
#include <ostream>

#include "datadog_conf.h"
#include "dd.h"
#include "ngx_event_scheduler.h"
#include "ngx_logger.h"
#include "string_util.h"

namespace datadog {
namespace nginx {
namespace {

const std::string_view DEFAULT_CONFIG = R"json({"service": "nginx"})json";

std::string_view or_default(std::string_view config_json) {
  if (config_json.empty()) {
    return DEFAULT_CONFIG;
  }
  return config_json;
}

}  // namespace

dd::Expected<dd::Tracer> TracingLibrary::make_tracer(
    const datadog_main_conf_t& nginx_conf) {
  dd::TracerConfig config;
  config.logger = std::make_shared<NgxLogger>();
  config.agent.event_scheduler = std::make_shared<NgxEventScheduler>();

  if (!nginx_conf.propagation_styles.empty()) {
    config.injection_styles = config.extraction_styles =
        nginx_conf.propagation_styles;
  }

  if (nginx_conf.service_name) {
    config.defaults.service = nginx_conf.service_name->value;
  } else {
    config.defaults.service = "nginx";
  }

  if (nginx_conf.environment) {
    config.defaults.environment = nginx_conf.environment->value;
  }

  if (nginx_conf.agent_url) {
    config.agent.url = nginx_conf.agent_url->value;
  }

  // Set sampling rules based on any `datadog_sample_rate` directives.
  std::vector<sampling_rule_t> rules = nginx_conf.sampling_rules;
  // Sort by descending depth, so that rules in a `location` block come before
  // those in a `server` block, before those in a `http` block.
  //
  // The sort is stable so that the relative order of rules within the same
  // depth is preserved.
  //
  // Strictly speaking, we don't need this sorting, because all of the rules
  // specify a distinct value for the "nginx.sample_rate_source" tag, and so the
  // order in which we try the rules doesn't change the outcome.
  // Deeper directives are more likely to match a given request, though, and
  // so this can be thought of as an optimization.
  const auto by_depth_descending = [](const auto& left, const auto& right) {
    return *left.depth > *right.depth;
  };
  std::stable_sort(rules.begin(), rules.end(), by_depth_descending);
  for (sampling_rule_t& rule : rules) {
    config.trace_sampler.rules.push_back(std::move(rule.rule));
  }

  auto final_config = dd::finalize_config(config);
  if (!final_config) {
    return final_config.error();
  }

  return dd::Tracer(*final_config);
}

dd::Expected<std::vector<std::string_view>>
TracingLibrary::propagation_header_names(
    const std::vector<dd::PropagationStyle>& configured_styles,
    dd::Logger& logger) {
  std::vector<std::string_view> result;

  // Create a tracer config that contains `configured_styles` (or the default
  // styles). Then finalize the config to obtain the final styles, which might
  // differ from `configured_styles` due to environment variables.
  dd::TracerConfig minimal_config;
  // A non-empty service name is required.
  minimal_config.defaults.service = "dummy";
  // Don't bother with a real collector.
  minimal_config.report_traces = false;
  // Empty `configured_styles` would mean "use the default."
  if (!configured_styles.empty()) {
    minimal_config.injection_styles = configured_styles;
    minimal_config.extraction_styles = configured_styles;
  }
  auto finalized_config = dd::finalize_config(minimal_config);
  if (auto* error = finalized_config.if_error()) {
    return std::move(*error);
  }

  if (!configured_styles.empty() &&
      configured_styles != finalized_config->injection_styles) {
    logger.log_error([&](std::ostream& log) {
      log << "Actual injection propagation styles differ from that specified "
             "in the nginx "
             "configuration.  The datadog_propagation_styles directive "
             "indicated the values "
          << dd::to_json(configured_styles)
          << ", but after applying environment variables, the final values are "
             "instead "
          << dd::to_json(finalized_config->injection_styles);
    });
  }

  // See `void TraceSegment::inject(DictWriter& writer, const SpanData& span)`
  // in dd-trace-cpp.
  for (const auto style : finalized_config->injection_styles) {
    switch (style) {
      case dd::PropagationStyle::DATADOG:
        result.push_back("x-datadog-trace-id");
        result.push_back("x-datadog-parent-id");
        result.push_back("x-datadog-sampling-priority");
        result.push_back("x-datadog-origin");
        result.push_back("x-datadog-tags");
        result.push_back("x-datadog-delegate-trace-sampling");
        break;
      case dd::PropagationStyle::B3:
        result.push_back("x-b3-traceid");
        result.push_back("x-b3-spanid");
        result.push_back("x-b3-sampled");
        break;
      case dd::PropagationStyle::W3C:
        result.push_back("traceparent");
        result.push_back("tracestate");
        break;
      case dd::PropagationStyle::NONE:
        break;
    }
  }

  return result;
}

std::string_view TracingLibrary::propagation_header_variable_name_prefix() {
  return "datadog_propagation_header_";
}

std::string_view TracingLibrary::environment_variable_name_prefix() {
  return "datadog_env_";
}

std::string_view TracingLibrary::configuration_json_variable_name() {
  return "datadog_config_json";
}

std::string_view TracingLibrary::location_variable_name() {
  return "datadog_location";
}

std::string_view TracingLibrary::proxy_directive_variable_name() {
  return "datadog_proxy_directive";
}

namespace {

class SpanContextJSONWriter : public dd::DictWriter {
  nlohmann::json output_object_;

 public:
  SpanContextJSONWriter() : output_object_(nlohmann::json::object()) {}

  void set(std::string_view key, std::string_view value) override {
    std::string normalized_key;
    std::transform(key.begin(), key.end(), std::back_inserter(normalized_key),
                   header_transform_char);
    output_object_[std::move(normalized_key)] = value;
  }

  nlohmann::json& json() { return output_object_; }
};

std::string span_property(std::string_view key, const dd::Span& span) {
  const auto not_found = "-";

  if (key == "trace_id") {
    return std::to_string(span.trace_id().low);
  } else if (key == "span_id") {
    return std::to_string(span.id());
  } else if (key == "json") {
    SpanContextJSONWriter writer;
    span.inject(writer);
    return writer.json().dump();
  }

  return not_found;
}

}  // namespace

NginxVariableFamily TracingLibrary::span_variables() {
  return {.prefix = "datadog_", .resolve = span_property};
}

std::vector<std::string_view> TracingLibrary::environment_variable_names() {
  return std::vector<std::string_view>{
      std::begin(dd::environment::variable_names),
      std::end(dd::environment::variable_names)};
}

std::string_view TracingLibrary::default_request_operation_name_pattern() {
  return "nginx.request";
}

std::string_view TracingLibrary::default_location_operation_name_pattern() {
  return "nginx.$datadog_proxy_directive";
}

std::unordered_map<std::string_view, std::string_view>
TracingLibrary::default_tags() {
  return {
      // originally defined by nginx-opentracing
      {"component", "nginx"},
      {"nginx.worker_pid", "$pid"},
      {"peer.address", "$remote_addr:$remote_port"},
      {"upstream.address", "$upstream_addr"},
      {"http.method", "$request_method"},
      {"http.url", "$scheme://$http_host$request_uri"},
      {"http.host", "$http_host"},
      // added by nginx-datadog
      // See
      // https://docs.datadoghq.com/logs/log_configuration/attributes_naming_convention/#common-attributes
      {"http.useragent", "$http_user_agent"},
      {"nginx.location", "$datadog_location"}};
}

std::string_view TracingLibrary::default_resource_name_pattern() {
  return "$request_method $uri";
}

bool TracingLibrary::tracing_on_by_default() { return true; }

bool TracingLibrary::trace_locations_by_default() { return false; }

std::string_view TracingLibrary::sampling_delegation_response_variable_name() {
  return "datadog_sampling_delegation_response";
}

}  // namespace nginx
}  // namespace datadog
