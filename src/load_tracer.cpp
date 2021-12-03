#include "load_tracer.h"
#include "ot.h"
#include "tracing_library.h"
#include "utility.h"

#include <datadog/opentracing.h>

namespace datadog {
namespace nginx {

ngx_int_t load_tracer(ngx_log_t* log, const char* /*tracer_library*/,
                      const char* config_file,
                      ot::DynamicTracingLibraryHandle& /*handle*/,
                      std::shared_ptr<ot::Tracer>& tracer) {
  std::string tracer_config;
  if (read_file(config_file, tracer_config)) {
    ngx_log_error(NGX_LOG_ERR, log, errno,
                  "Failed to read tracer configuration file %s", config_file);
    return NGX_ERROR;
  }

  std::string error;
  const auto new_tracer = TracingLibrary::make_tracer(tracer_config, error);
  if (new_tracer == nullptr) {
    ngx_log_error(NGX_LOG_ERR, log, 0, "Failed to construct tracer: %s", error.c_str());
    return NGX_ERROR;
  }

  tracer = std::move(new_tracer);
  return NGX_OK;
}

}  // namespace nginx
}  // namespace datadog
