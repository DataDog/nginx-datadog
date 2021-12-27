#pragma once

#include "string_view.h"


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

extern const string_view opentracing_context_variable_name;

ngx_int_t add_variables(ngx_conf_t* cf) noexcept;

}  // namespace nginx
}  // namespace datadog
