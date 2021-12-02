#include "tracing_library.h"
#include "utility.h"

#include <datadog/opentracing.h>
#include <opentracing/expected/expected.hpp>
#include <opentracing/tracer_factory.h>

extern "C" {
#include <ngx_core.h>
#include <ngx_log.h>
}

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

void log_to_nginx(::datadog::opentracing::LogLevel level, ot::string_view message) {
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
    ngx_log_error(ngx_level, ngx_cycle->log, 0, "datadog: %V", &ngx_message);
}

} // namespace

std::shared_ptr<ot::Tracer> TracingLibrary::make_tracer(ot::string_view configuration, std::string &error) {
    auto maybe_options = ::datadog::opentracing::optionsFromConfig(std::string(configuration).c_str(), error);
    if (!maybe_options) {
        if (error.empty()) {
            error = "unable to parse options from config";
        }
        return nullptr;
    }

    // Use nginx's logger, instead of the default standard error.
    maybe_options->log_func = log_to_nginx;

    return ::datadog::opentracing::makeTracer(*maybe_options);
}

std::vector<ot::string_view> TracingLibrary::span_tag_names(ot::string_view configuration, std::string &error) {
    const auto maybe_options = ::datadog::opentracing::optionsFromConfig(std::string(configuration).c_str(), error);
    if (!maybe_options) {
        if (error.empty()) {
            error = "unable to parse options from config";
        }
        return {};
    }

    const bool prioritySamplingEnabled = true;
    return ::datadog::opentracing::getPropagationHeaderNames(maybe_options->inject, prioritySamplingEnabled);
}

std::vector<ot::string_view> TracingLibrary::environment_variable_names() {
    return {
        // These environment variable names are taken from `tracer_options.cpp`
        // and `tracer.cpp` in the `dd-opentracing-cpp` repository.
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
    
bool TracingLibrary::tracing_on_by_default() {
    return true;
}

bool TracingLibrary::trace_locations_by_default() {
    return false;
}

bool TracingLibrary::configure_tracer_json_inline() {
    return true;
}

} // namespace nginx
} // namespace datadog
