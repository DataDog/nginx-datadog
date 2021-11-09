#include "load_tracer.h"
#include "ot.h"


#include <opentracing/dynamic_load.h>
#include <cassert>
#include <cerrno>
#include <fstream>
#include <datadog/opentracing.h>

namespace datadog {
namespace opentracing {
// This function is defined in `dd-opentracing-cpp`.
int OpenTracingMakeTracerFactoryFunction(const char* opentracing_version,
                                         const char* opentracing_abi_version,
                                         const void** error_category, void* error_message,
                                         void** tracer_factory);
}  // namespace opentracing

namespace nginx {
namespace {
ngx_int_t load_datadog_tracer(const ot::TracerFactory*& factory) {
  std::string error_message;
  const void* error_category = nullptr;
  void* tracer_factory = nullptr;
  const int rcode = ::datadog::opentracing::OpenTracingMakeTracerFactoryFunction(
      OPENTRACING_VERSION, OPENTRACING_ABI_VERSION, &error_category,
      static_cast<void*>(&error_message), &tracer_factory);
  if (rcode) {
    // TODO: diagnostic
    return NGX_ERROR;
  }
  if (tracer_factory == nullptr) {
    // TODO: diagnostic
    return NGX_ERROR;
  }

  factory = static_cast<const ot::TracerFactory*>(tracer_factory);
  return NGX_OK;
}
}  // namespace

ngx_int_t load_tracer(ngx_log_t* log, const char* /*tracer_library*/,
                      const char* config_file,
                      ot::DynamicTracingLibraryHandle& handle,
                      std::shared_ptr<ot::Tracer>& tracer) {
  const ot::TracerFactory* tracer_factory;
  if (const ngx_int_t rcode = load_datadog_tracer(tracer_factory)) {
    return rcode;
  }
  assert(tracer_factory);

  // Construct a tracer
  std::ifstream in{config_file};
  if (!in.good()) {
    ngx_log_error(NGX_LOG_ERR, log, errno,
                  "Failed to open tracer configuration file %s", config_file);
    return NGX_ERROR;
  }
  std::string tracer_config{std::istreambuf_iterator<char>{in},
                            std::istreambuf_iterator<char>{}};
  if (!in.good()) {
    ngx_log_error(NGX_LOG_ERR, log, errno,
                  "Failed to read tracer configuration file %s", &config_file);
    return NGX_ERROR;
  }

  std::string error_message;
  auto tracer_maybe =
      tracer_factory->MakeTracer(tracer_config.c_str(), error_message);
  if (!tracer_maybe) {
    if (!error_message.empty()) {
      ngx_log_error(NGX_LOG_ERR, log, 0, "Failed to construct tracer: %s",
                    error_message.c_str());
    } else {
      ngx_log_error(NGX_LOG_ERR, log, 0, "Failed to construct tracer: %s",
                    tracer_maybe.error().message().c_str());
    }
    return NGX_ERROR;
  }

  tracer = std::move(*tracer_maybe);
  return NGX_OK;
}
}  // namespace nginx
}  // namespace datadog
