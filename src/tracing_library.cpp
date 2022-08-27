#include "tracing_library.h"

#include "string_util.h"

#include <datadog/error.h>
#include <datadog/expected.h>
#include <datadog/logger.h>
#include <datadog/tracer.h>
#include <datadog/tracer_config.h>
#include <datadog/span.h>

extern "C" {
#include <ngx_core.h>
#include <ngx_log.h>
}

#include <algorithm>
#include <iterator>
#include <memory>
#include <mutex>
#include <sstream>

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

class NginxLogger : public dd::Logger {
  std::mutex mutex_;

 public:
  void log_error(const dd::Logger::LogFunc& write) override {
    std::ostringstream stream;
    write(stream);
    log_error(stream.str());
  }

  void log_startup(const dd::Logger::LogFunc& write) override {
    std::ostringstream stream;
    write(stream);
    const std::string message = stream.str();
    const ngx_str_t ngx_message = to_ngx_str(buffer);

    std::lock_guard<std::mutex> lock(mutex_);
    (void) ngx_write_fd(ngx_stderr, ngx_message.data(), ngx_message.size());
  }

  void log_error(const dd::Error& error) override {
    const ngx_str_t ngx_message = to_ngx_str(error.message);
    
    std::lock_guard<std::mutex> lock(mutex_);
    ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "datadog: [error code %d] %V", int(error.code), &ngx_message);
  }

  void log_error(std::string_view message) override {
    const ngx_str_t ngx_message = to_ngx_str(message);
    
    std::lock_guard<std::mutex> lock(mutex_);
    ngx_log_error(NGX_LOG_ERR, ngx_cycle->log, 0, "datadog: %V", &ngx_message);
  }
};

}  // namespace

dd::Expected<dd::Tracer> TracingLibrary::make_tracer(std::string_view json_config) {
  const std::string config_str = or_default(json_config);
  // TODO: create a `dd::TracerConfig` from the JSON.
  (void) config_str;

  dd::TracerConfig config;
  config.defaults.service = "dd-trace-cpp-nginx";
  config.logger = std::make_shared<NgxLogger>();
  
  auto final_config = dd::finalize_config(config);
  if (!final_config) {
    return final_config.error();
  }

  return dd::Tracer(final_config);
}

std::vector<std::string_view> TracingLibrary::propagation_header_names(std::string_view configuration,
                                                                  std::string &error) {
  // TODO: Parse `configuration` to figure out the propagation styles, and then
  // get the corresponding upstream request header names.
  // For now, I hard-code for the Datadog style.
  (void)configuration;
  (void)error;
  
  return {
    "x-datadog-trace-id",
    "x-datadog-parent-id",
    "x-datadog-sampling-priority",
    "x-datadog-origin",
    };
}

string_view TracingLibrary::propagation_header_variable_name_prefix() {
  return "datadog_propagation_header_";
}

string_view TracingLibrary::environment_variable_name_prefix() { return "datadog_env_"; }

string_view TracingLibrary::configuration_json_variable_name() { return "datadog_config_json"; }

string_view TracingLibrary::location_variable_name() { return "datadog_location"; }

string_view TracingLibrary::proxy_directive_variable_name() { return "datadog_proxy_directive"; }

namespace {

std::string span_property(std::string_view key, const dd::Span &span) {
  const auto not_found = "-";

  if (key == "trace_id") {
    return std::to_string(span.trace_id());
  } else if (key == "span_id") {
    return std::to_string(span.id());
  } else if (key == "json") {
    // TODO
    return not_found;
  }

  return not_found;
}

}  // namespace

NginxVariableFamily TracingLibrary::span_variables() {
  return {.prefix = "datadog_", .resolve = span_property};
}

std::vector<std::string_view> TracingLibrary::environment_variable_names() {
  return {// These environment variable names are taken from `tracer_options.cpp`
          // and `tracer.cpp` in the `dd-opentracing-cpp` repository.
          // I did `git grep '"DD_\w\+"' -- src/` in the `dd-opentracing-cpp`
          // repository.
          "DD_AGENT_HOST",
          "DD_ENV",
          "DD_PROPAGATION_STYLE_EXTRACT",
          "DD_PROPAGATION_STYLE_INJECT",
          "DD_SERVICE",
          "DD_TAGS",
          "DD_TRACE_AGENT_PORT",
          "DD_TRACE_AGENT_URL",
          "DD_TRACE_ANALYTICS_ENABLED",
          "DD_TRACE_ANALYTICS_SAMPLE_RATE",
          "DD_TRACE_CPP_LEGACY_OBFUSCATION",
          "DD_TRACE_DEBUG",
          "DD_TRACE_ENABLED",
          "DD_TRACE_RATE_LIMIT",
          "DD_TRACE_REPORT_HOSTNAME",
          "DD_TRACE_SAMPLE_RATE",
          "DD_TRACE_SAMPLING_RULES",
          "DD_TRACE_STARTUP_LOGS",
          "DD_TRACE_TAGS_PROPAGATION_MAX_LENGTH",
          "DD_VERSION"};
}

string_view TracingLibrary::default_request_operation_name_pattern() { return "nginx.request"; }

string_view TracingLibrary::default_location_operation_name_pattern() {
  return "nginx.$datadog_proxy_directive";
}

std::unordered_map<std::string_view, std::string_view> TracingLibrary::default_tags() {
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

string_view TracingLibrary::default_resource_name_pattern() { return "$request_method $uri"; }

bool TracingLibrary::tracing_on_by_default() { return true; }

bool TracingLibrary::trace_locations_by_default() { return false; }

std::string TracingLibrary::configuration_json(const dd::Tracer &tracer) {
  // TODO
  (void)tracer;
  return "{\"implemented\": \"not\"}";
}

}  // namespace nginx
}  // namespace datadog
