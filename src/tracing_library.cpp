#include "tracing_library.h"

#include <datadog/opentracing.h>
#include <opentracing/expected/expected.hpp>
#include <opentracing/tracer_factory.h>

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

// Return a Datadog tracer factory, or return `nullptr` if an error occurs.
std::unique_ptr<const ot::TracerFactory> load_datadog_tracer_factory() {
  std::string error_message;
  const void* error_category = nullptr;
  void* tracer_factory = nullptr;
  const int rcode = ::datadog::opentracing::OpenTracingMakeTracerFactoryFunction(
      OPENTRACING_VERSION, OPENTRACING_ABI_VERSION, &error_category,
      static_cast<void*>(&error_message), &tracer_factory);
  if (rcode) {
    // TODO: diagnostic?
    return nullptr;
  }
  if (tracer_factory == nullptr) {
    // TODO: diagnostic?
    return nullptr;
  }

  return std::unique_ptr<const ot::TracerFactory>(static_cast<const ot::TracerFactory*>(tracer_factory));
}

} // namespace

std::shared_ptr<ot::Tracer> TracingLibrary::make_tracer(ot::string_view configuration, std::string &error) {
    const auto factory = load_datadog_tracer_factory();
    if (factory == nullptr) {
        return nullptr;
    }

    const auto maybe_tracer = factory->MakeTracer(std::string(configuration).c_str(), error);
    if (!maybe_tracer) {
        return nullptr;
    }

    return *maybe_tracer;
}

std::vector<std::string> TracingLibrary::span_tag_names(ot::string_view configuration, std::string &error) {
    const auto maybe_options = ::datadog::opentracing::optionsFromConfig(std::string(configuration).c_str(), error);
    if (!maybe_options) {
        return {};
    }

    const bool prioritySamplingEnabled = true;
    const auto string_views = ::datadog::opentracing::getPropagationHeaderNames(maybe_options->inject, prioritySamplingEnabled);
    return std::vector<std::string>{string_views.begin(), string_views.end()};
}

std::vector<std::string> TracingLibrary::environment_variable_names() {
    return {
        // These environment variable names are taken from `tracer_options.cpp`
        // in the `dd-opentracing-cpp` repository.
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
        "DD_TRACE_REPORT_HOSTNAME",
        "DD_TRACE_SAMPLING_RULES",
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
