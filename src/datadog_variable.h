#pragma once

#include <string_view>

extern "C" {
#include <nginx.h>
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
}

#include <string>
#include <utility>
#include <vector>

namespace datadog {
namespace nginx {

// Define configuration variables that can be used in the specified
// configuration `cf`.  The names of these variable are determined by
// corresponding static functions in `TracingLibrary`.
ngx_int_t add_variables(ngx_conf_t* cf) noexcept;

}  // namespace nginx
}  // namespace datadog
