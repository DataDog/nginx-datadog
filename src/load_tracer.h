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

// Return a tracer instance configured using the specified `tracer_config`, or
// return `nullptr` if an error occurs.  If an error occurs, log a diagnostic
// using the specified `log`.
std::shared_ptr<ot::Tracer> load_tracer(ngx_log_t* log, ot::string_view tracer_config);

}  // namespace nginx
}  // namespace datadog
