#pragma once

extern "C" {
#include <nginx.h>
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
}

namespace datadog {
namespace nginx {

// Return an nginx array whose elements are `ot::string_view` objects referring
// to the names of span tags injected for context propagation.  Use the
// specified `pool` to supply buffers that the `ot::string_view` objects will
// refer to.  Determine the names of the relevant span tags by loading the
// tracer configuration from the specified `tracer_config_file`.  If an error
// occurs, print a diagnostic to the specified `log` and return `nullptr`.
ngx_array_t* discover_span_context_keys(ngx_pool_t* pool, ngx_log_t* log, const char* tracer_config_file);

}  // namespace nginx
}  // namespace datadog
