#pragma once

#include "ngx_script.h"

extern "C" {
#include <nginx.h>
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
}

namespace datadog {
namespace nginx {
struct datadog_tag_t {
  NgxScript key_script;
  NgxScript value_script;
};

struct datadog_main_conf_t {
  ngx_array_t *tags;
  ngx_str_t tracer_library; // TODO: not needed
  ngx_str_t tracer_conf_file; // TODO: use file contents instead
  ngx_array_t *span_context_keys;
};

struct datadog_loc_conf_t {
  ngx_flag_t enable;
  ngx_flag_t enable_locations;
  NgxScript operation_name_script; // TODO: default value "$request_method $uri"?
  NgxScript loc_operation_name_script; // TODO: default value "$request_method $uri"?
  ngx_flag_t trust_incoming_span;
  ngx_array_t *tags;
  NgxScript response_info_script;
};
}  // namespace nginx
}  // namespace datadog
