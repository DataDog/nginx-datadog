#pragma once

#include <string_view>

extern "C" {
#include <nginx.h>
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
}

namespace datadog {
namespace nginx {

// Return an nginx array whose elements are `std::string_view` objects referring
// to the names of span tags injected for context propagation.  Use the
// specified `pool` to supply memory.  Determine the names of the relevant span
// tags by consulting a tracer configuration using the specified
// `tracer_config`.  If an error occurs, print a diagnostic to the specified
// `log` and return `nullptr`.
ngx_array_t* discover_span_context_keys(ngx_pool_t* pool, ngx_log_t* log,
                                        std::string_view tracer_config);

}  // namespace nginx
}  // namespace datadog
