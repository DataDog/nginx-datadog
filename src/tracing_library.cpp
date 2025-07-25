#include "tracing_library.h"

#include <datadog/dict_writer.h>
#include <datadog/environment.h>
#include <datadog/error.h>
#include <datadog/expected.h>
#include <datadog/span.h>
#include <datadog/tracer.h>
#include <datadog/tracer_config.h>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <iterator>

extern "C" {
#include <inttypes.h>
}

#include "datadog_conf.h"
#include "ngx_event_scheduler.h"
#ifdef WITH_WAF
#include "security/waf_remote_cfg.h"
#endif
#include "nginx_flavors.h"
#include "string_util.h"

namespace datadog {
namespace nginx {

inline constexpr std::string_view integration_name_from_flavor(
    flavor nginx_flavor) {
  switch (nginx_flavor) {
    case flavor::vanilla:
      return "nginx";
    case flavor::openresty:
      return "nginx:openresty";
    case flavor::ingress_nginx:
      return "nginx:ingress-nginx";
  }

  static_assert(true, "unknown NGINX flavor");
  std::abort();
}

dd::Expected<dd::Tracer> TracingLibrary::make_tracer(
    const datadog_main_conf_t &nginx_conf, std::shared_ptr<dd::Logger> logger) {
  dd::TracerConfig config;
  config.logger = std::move(logger);
  config.agent.event_scheduler = std::make_shared<NgxEventScheduler>();
  config.integration_name = integration_name_from_flavor(kNginx_flavor);
  config.integration_version = NGINX_VERSION;
  config.service = "nginx";

  if (nginx_conf.apm_tracing_enabled != NGX_CONF_UNSET) {
    config.tracing_enabled = {nginx_conf.apm_tracing_enabled == 1};
  }

  if (!nginx_conf.propagation_styles.empty()) {
    config.injection_styles = config.extraction_styles =
        nginx_conf.propagation_styles;
  }

  if (nginx_conf.agent_url) {
    config.agent.url = nginx_conf.agent_url;
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
  const auto by_depth_descending = [](const auto &left, const auto &right) {
    return *left.depth > *right.depth;
  };
  std::stable_sort(rules.begin(), rules.end(), by_depth_descending);
  for (sampling_rule_t &rule : rules) {
    config.trace_sampler.rules.push_back(std::move(rule.rule));
  }

#ifdef WITH_WAF
  const bool appsec_fully_disabled = (nginx_conf.appsec_enabled == 0);
  if (!appsec_fully_disabled) {
    const bool has_custom_ruleset = (nginx_conf.appsec_ruleset_file.len > 0);
    const bool appsec_enabling_explicit =
        (nginx_conf.appsec_enabled != NGX_CONF_UNSET);
    security::register_with_remote_cfg(
        config.agent,
        !has_custom_ruleset,         // no custom ruleset => ruleset via rem cfg
        !appsec_enabling_explicit);  // no explicit => control via rem cfg
  }
#endif

  auto final_config = dd::finalize_config(config);

  if constexpr (kNginx_flavor == nginx::flavor::ingress_nginx) {
    // NOTE(@dmehala): ingress-nginx regularly poll an healthcheck endpoint.
    // To avoid reporting traces, set the sampling rate to `0` for this
    // endpoint. This is done after `finalize_config` because it can
    // environment variables override the programmatic configuration.
    final_config->trace_sampler.rules.emplace_back(
        datadog::tracing::TraceSamplerRule{
            .rate = datadog::tracing::Rate::zero(),
            .matcher =
                datadog::tracing::SpanMatcher{
                    .service = "*",
                    .name = "*",
                    .resource = "GET /is-dynamic-lb-initialized",
                    .tags = {}},
            .mechanism = datadog::tracing::SamplingMechanism::RULE});
  }

  if (!final_config) {
    return final_config.error();
  }

  return dd::Tracer(*final_config);
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

namespace {

class SpanContextJSONWriter : public dd::DictWriter {
  rapidjson::Document output_object_;

 public:
  SpanContextJSONWriter() : output_object_() { output_object_.SetObject(); }

  void set(std::string_view key, std::string_view value) override {
    std::string normalized_key;
    std::transform(key.begin(), key.end(), std::back_inserter(normalized_key),
                   header_transform_char);

    rapidjson::Document::AllocatorType &allocator =
        output_object_.GetAllocator();
    output_object_.AddMember(
        rapidjson::Value(normalized_key.c_str(), allocator).Move(),
        rapidjson::Value(value.data(), allocator).Move(), allocator);
  }

  rapidjson::Document &json() { return output_object_; }
};

std::string span_property(std::string_view key, const dd::Span &span) {
  const auto not_found = "-";

  if (key == "trace_id_hex" || key == "trace_id") {
    return span.trace_id().hex_padded();
  } else if (key == "span_id_hex" || key == "span_id") {
    char buffer[17];
    int written =
        std::snprintf(buffer, sizeof(buffer), "%016" PRIx64, span.id());
    assert(written == 16);
    return {buffer, static_cast<size_t>(written)};
  } else if (key == "trace_id_64bits_base10") {
    return std::to_string(span.trace_id().low);
  } else if (key == "span_id_64bits_base10") {
    return std::to_string(span.id());
  } else if (key == "json") {
    SpanContextJSONWriter writer;
    span.inject(writer);

    auto &json_doc = writer.json();

    rapidjson::StringBuffer buffer;
    rapidjson::Writer<rapidjson::StringBuffer> buffer_writer(buffer);
    json_doc.Accept(buffer_writer);
    return buffer.GetString();
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
  return "nginx.location";
}

std::unordered_map<std::string_view, std::string_view>
TracingLibrary::default_tags() {
  static const std::unordered_map<std::string_view, std::string_view> tags{
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
      // <https://docs.datadoghq.com/logs/log_configuration/attributes_naming_convention/#common-attributes>
      {"http.useragent", "$http_user_agent"},
      {"nginx.location", "$datadog_location"}};
  return tags;
}

std::string_view TracingLibrary::default_resource_name_pattern() {
  return "$request_method $uri";
}

bool TracingLibrary::tracing_on_by_default() { return true; }

bool TracingLibrary::trace_locations_by_default() { return false; }

}  // namespace nginx
}  // namespace datadog
