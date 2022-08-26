#include "tracing_library.h"



#include "string_util.h"

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
namespace opentracing {

// This function is defined in the `dd-opentracing-cpp` repository.
ot::expected<TracerOptions> optionsFromConfig(const char *configuration,
                                              std::string &error_message);

// This function is defined in the `dd-opentracing-cpp` repository.
std::vector<ot::string_view> getPropagationHeaderNames(const std::set<PropagationStyle> &styles,
                                                       bool prioritySamplingEnabled);

// This function is defined in the `dd-opentracing-cpp` repository.
std::string getConfigurationJSON(const ot::Tracer &tracer);

}  // namespace opentracing

namespace nginx {
namespace {

const std::string_view DEFAULT_CONFIG = R"json({"service": "nginx"})json";

string_view or_default(std::string_view config_json) {
  if (config_json.empty()) {
    return DEFAULT_CONFIG;
  }
  return config_json;
}

// This function-like object logs to nginx's error log when invoked.  It also
// manages a mutex to serialize access to the log.
class NginxLogFunc {
  // The mutex is referred to by a `shared_ptr` because the `TracerOptions`
  // object that will contain this function can be copied.
  std::shared_ptr<std::mutex> mutex_;

 public:
  NginxLogFunc() : mutex_(std::make_shared<std::mutex>()) {}

  void operator()(::datadog::opentracing::LogLevel level, std::string_view message) {
    int ngx_level = NGX_LOG_STDERR;

    switch (level) {
      case ::datadog::opentracing::LogLevel::debug:
        ngx_level = NGX_LOG_DEBUG;
        break;
      case ::datadog::opentracing::LogLevel::info:
        ngx_level = NGX_LOG_INFO;
        break;
      case ::datadog::opentracing::LogLevel::error:
        ngx_level = NGX_LOG_ERR;
        break;
    }

    const ngx_str_t ngx_message = to_ngx_str(message);
    std::lock_guard<std::mutex> guard(*mutex_);
    ngx_log_error(ngx_level, ngx_cycle->log, 0, "datadog: %V", &ngx_message);
  }
};

}  // namespace

std::shared_ptr<ot::Tracer> TracingLibrary::make_tracer(std::string_view configuration,
                                                        std::string &error) {
  const std::string config_str = or_default(configuration);
  auto maybe_options = ::datadog::opentracing::optionsFromConfig(config_str.c_str(), error);
  if (!maybe_options) {
    if (error.empty()) {
      error = "unable to parse options from config";
    }
    return nullptr;
  }

  // Use nginx's logger, instead of the default standard error.
  maybe_options->log_func = NginxLogFunc();

  return ::datadog::opentracing::makeTracer(*maybe_options);
}

std::vector<string_view> TracingLibrary::propagation_header_names(std::string_view configuration,
                                                                  std::string &error) {
  const std::string config_str = or_default(configuration);
  auto maybe_options = ::datadog::opentracing::optionsFromConfig(config_str.c_str(), error);
  if (!maybe_options) {
    if (error.empty()) {
      error = "unable to parse options from config";
    }
    return {};
  }

  const bool priority_sampling_enabled = true;
  return ::datadog::opentracing::getPropagationHeaderNames(maybe_options->inject,
                                                           priority_sampling_enabled);
}

string_view TracingLibrary::propagation_header_variable_name_prefix() {
  return "datadog_propagation_header_";
}

string_view TracingLibrary::environment_variable_name_prefix() { return "datadog_env_"; }

string_view TracingLibrary::configuration_json_variable_name() { return "datadog_config_json"; }

string_view TracingLibrary::location_variable_name() { return "datadog_location"; }

string_view TracingLibrary::proxy_directive_variable_name() { return "datadog_proxy_directive"; }

namespace {

std::string span_property(std::string_view key, const ot::Span &span) {
  const auto not_found = "-";

  if (key == "trace_id") {
    return span.context().ToTraceID();
  } else if (key == "span_id") {
    return span.context().ToSpanID();
  } else if (key == "json") {
    std::ostringstream carrier;
    const auto result = span.tracer().Inject(span.context(), carrier);
    if (!result) {
      return not_found;
    }
    return carrier.str();
  }

  return not_found;
}

}  // namespace

NginxVariableFamily TracingLibrary::span_variables() {
  return {.prefix = "datadog_", .resolve = span_property};
}

std::vector<string_view> TracingLibrary::environment_variable_names() {
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

std::unordered_map<string_view, std::string_view> TracingLibrary::default_tags() {
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

std::string TracingLibrary::configuration_json(const ot::Tracer &tracer) {
  const bool with_timestamp = false;
  return datadog::opentracing::toJSON(datadog::opentracing::getOptions(tracer), with_timestamp);
}

}  // namespace nginx
}  // namespace datadog
