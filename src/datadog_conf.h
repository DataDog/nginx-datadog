#pragma once

#include "ngx_script.h"
#include "ot.h"

extern "C" {
#include <nginx.h>
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
}

#include <opentracing/span.h>

#include <string>

namespace datadog {
namespace nginx {

struct datadog_tag_t {
  NgxScript key_script;
  NgxScript value_script;
};

struct conf_directive_source_location {
  ngx_str_t file_name;       // e.g. "nginx.conf"
  ngx_uint_t line;           // line number within the file `file_name`
  ngx_str_t directive_name;  // e.g. "proxy_pass"
};

struct datadog_main_conf_t {
  ngx_array_t *tags;
  // `is_tracer_configured` is whether the tracer has been configured, either
  // by an explicit `datadog_configure` directive, or implicitly to a default
  // configuration by another directive.
  bool is_tracer_configured;
  // `tracer_conf` is the text of the tracer's configuration, either from the
  // nginx configuration file or from a separately loaded file.  If
  // `tracer_conf` is empty, then a default configuration is used.
  ngx_str_t tracer_conf;
  // `tracer_conf_source_location` is the source location of the configuration
  // directive that caused the tracer configuration to be loaded.  The
  // `datadog_configure` and `datadog` blocks cause the tracer configuration to
  // be loaded, but there are other directives that cause a default
  // configuration to be loaded if no other configuration has yet been loaded.
  // The purpose of `tracer_conf_source_location` is to enable the error
  // diagnostic:
  // > Configuration already loaded to default configuration by
  // > [[source location]].  Explicit configuration must appear before
  // > the first [[directive name]].
  conf_directive_source_location tracer_conf_source_location;
  // `are_log_formats_defined` is whether we have already injected `log_format`
  // directives into the configuration.  The directives define Datadog-specific
  // access log formats; one of which will override nginx's default.
  // `are_log_formats_defined` allows us to ensure that the log formats are
  // defined exactly once, even though they may be defined in multiple contexts
  // (e.g. before the first `server` block, before the first `access_log`
  // directive).
  bool are_log_formats_defined;
  ngx_array_t *span_context_keys;
};

struct datadog_loc_conf_t {
  ngx_flag_t enable;
  ngx_flag_t enable_locations;
  NgxScript operation_name_script;
  NgxScript loc_operation_name_script;
  ngx_flag_t trust_incoming_span;
  ngx_array_t *tags;
  NgxScript response_info_script;
};

}  // namespace nginx
}  // namespace datadog
