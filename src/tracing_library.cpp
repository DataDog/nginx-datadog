#include "tracing_library.h"
#include "utility.h"

#include <datadog/opentracing.h>
#include <opentracing/expected/expected.hpp>
#include <opentracing/tracer_factory.h>

extern "C" {
#include <ngx_core.h>
#include <ngx_log.h>
}

#include <memory>
#include <mutex>

namespace datadog {
namespace opentracing {
// This function is defined in the `dd-opentracing-cpp` repository.
int OpenTracingMakeTracerFactoryFunction(const char* opentracing_version,
                                         const char* opentracing_abi_version,
                                         const void** error_category, void* error_message,
                                         void** tracer_factory);

// This function is defined in the `dd-opentracing-cpp` repository.
ot::expected<TracerOptions> optionsFromConfig(const char *configuration, std::string &error_message);

// This function is defined in the `dd-opentracing-cpp` repository.
std::vector<ot::string_view> getPropagationHeaderNames(const std::set<PropagationStyle> &styles, bool prioritySamplingEnabled);

}  // namespace opentracing

namespace nginx {
namespace {

const string_view DEFAULT_CONFIG = R"json({"service": "nginx"})json";

string_view or_default(string_view config_json) {
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

    void operator()(::datadog::opentracing::LogLevel level, string_view message) {
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

} // namespace

std::shared_ptr<ot::Tracer> TracingLibrary::make_tracer(string_view configuration, std::string &error) {
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

std::vector<string_view> TracingLibrary::span_tag_names(string_view configuration, std::string &error) {
    const std::string config_str = or_default(configuration);
    auto maybe_options = ::datadog::opentracing::optionsFromConfig(config_str.c_str(), error);
    if (!maybe_options) {
        if (error.empty()) {
            error = "unable to parse options from config";
        }
        return {};
    }

    const bool priority_sampling_enabled = true;
    return ::datadog::opentracing::getPropagationHeaderNames(maybe_options->inject, priority_sampling_enabled);
}

std::vector<string_view> TracingLibrary::environment_variable_names() {
    return {
        // These environment variable names are taken from `tracer_options.cpp`
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
        "DD_TRACE_REPORT_HOSTNAME",
        "DD_TRACE_SAMPLING_RULES",
        "DD_TRACE_STARTUP_LOGS",
        "DD_VERSION"
    };
}
    
string_view TracingLibrary::default_operation_name_pattern() {
    return "$request_method $uri";
}

std::unordered_map<string_view, string_view> TracingLibrary::default_tags() {
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
        {"http_user_agent", "$http_user_agent"}
    };
}

bool TracingLibrary::tracing_on_by_default() {
    return true;
}

bool TracingLibrary::trace_locations_by_default() {
    return false;
}

} // namespace nginx
} // namespace datadog
