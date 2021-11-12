#pragma once

// The nginx module object itself, `ngx_module_t ngx_http_datadog_module` is
// defined in this translation unit.
//
// This header file exists to `extern` declare the module.  Other files can
// refer to the `ngx_http_datadog_module` variable by including this header.

extern "C" {
#include <ngx_core.h>

extern ngx_module_t ngx_http_datadog_module;
}
