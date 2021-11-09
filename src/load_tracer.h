#pragma once

#include <opentracing/dynamic_load.h>
#include "ot.h"


extern "C" {
#include <nginx.h>
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
}

namespace datadog {
namespace nginx {
ngx_int_t load_tracer(ngx_log_t* log, const char* tracer_library,
                      const char* config_file,
                      ot::DynamicTracingLibraryHandle& handle,
                      std::shared_ptr<ot::Tracer>& tracer);
}  // namespace nginx
}  // namespace datadog
