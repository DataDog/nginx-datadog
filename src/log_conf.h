#pragma once

// This component provides functions used to modify nginx's logging
// configuration.
// The Datadog module defines tracing-aware logging formats, and changes
// nginx's default format to one of the tracing-aware formats.
// In order to change the default, the tracing-aware logging formats must
// be defined before any implicit reference to the default format.  This can
// happen in multiple contexts, and so the common code is here.

extern "C" {
#include <nginx.h>
#include <ngx_config.h>
#include <ngx_core.h>
}

namespace datadog {
namespace nginx {

// Alter the specified `conf`, which is actively being parsed, so that the
// resulting logging configuration is as if Datadog-specific log formats had
// been specified in the configuration file, e.g.
//
//     log_format datadog_text ...;
//     log_format datadog_json ...;
//
// Alter `conf` only if such alterations have not already been made.  Return
// `NGX_OK` on success, or another value if an error occurs.  The behavior
// is undefined unless this module's main configuration has already been
// instantiated and associated with `conf`; i.e. configuration parsing has
// already progressed to inside of the `http` block.
ngx_int_t inject_datadog_log_formats(ngx_conf_t *conf);

}  // namespace nginx
}  // namespace datadog
