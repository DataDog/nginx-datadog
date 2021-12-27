#include "load_tracer.h"
#include "string_view.h"
#include "tracing_library.h"
#include "utility.h"

#include <datadog/opentracing.h>

namespace datadog {
namespace nginx {

std::shared_ptr<ot::Tracer> load_tracer(ngx_log_t* log, string_view tracer_config) {
  std::string error;
  auto tracer = TracingLibrary::make_tracer(tracer_config, error);
  if (tracer == nullptr) {
    ngx_log_error(NGX_LOG_ERR, log, 0, "Failed to construct tracer: %s", error.c_str());
  }

  return tracer;
}

}  // namespace nginx
}  // namespace datadog
