#pragma once

#include "datadog_conf.h"
#include "datadog_directive.h"

extern "C" {

#include <nginx.h>
#include <ngx_conf_file.h>
#include <ngx_config.h>
#include <ngx_core.h>
}

namespace datadog::nginx {

// Part of configuring a command is saying where the command is allowed to
// appear, e.g. in the `server` block, in a `location` block, etc.
// There are two sets of places Datadog commands can appear: either "anywhere,"
// or "anywhere but in the main section."  `anywhere` and `anywhere_but_main`
// are respective shorthands.
// Also, this definition of "anywhere" excludes "if" blocks. "if" blocks
// do not behave as many users expect, and it might be a technical liability to
// support them. See
// <https://www.nginx.com/resources/wiki/start/topics/depth/ifisevil/>
// and <http://nginx.org/en/docs/http/ngx_http_rewrite_module.html#if>
// and <http://agentzh.blogspot.com/2011/03/how-nginx-location-if-works.html>.
//
constexpr ngx_uint_t anywhere_but_main =
    NGX_HTTP_SRV_CONF     // an `http` block
    | NGX_HTTP_LOC_CONF;  // a `location` block (within an `http` block)

constexpr ngx_uint_t anywhere =
    anywhere_but_main | NGX_HTTP_MAIN_CONF;  // the toplevel configuration, e.g.
                                             // where modules are loaded

// Each "datadog_*" directive has a corresponding "opentracing_*" alias that
// logs a warning and then delegates to the "datadog_*" version, e.g.
// "opentracing_trace_locations" logs a warning and then calls
// "datadog_trace_locations".  The `ngx_command_t::type` bitmask of the two
// versions must match.  To ensure this, `DEFINE_COMMAND_WITH_OLD_ALIAS` is a
// macro that defines both commands at the same time.
#define DEFINE_COMMAND_WITH_OLD_ALIAS(NAME, OLD_NAME, TYPE, SET, CONF, OFFSET, \
                                      POST)                                    \
  {NAME, TYPE, SET, CONF, OFFSET, POST}, {                                     \
    OLD_NAME, TYPE, delegate_to_datadog_directive_with_warning,                \
        NGX_HTTP_LOC_CONF_OFFSET, 0, nullptr                                   \
  }

#define DEFINE_DEPRECATED_COMMAND_1_2_0(NAME, TYPE)                         \
  {                                                                         \
    NAME, TYPE, warn_deprecated_command_1_2_0, NGX_HTTP_LOC_CONF_OFFSET, 0, \
        NULL                                                                \
  }

char *delegate_to_datadog_directive_with_warning(ngx_conf_t *cf,
                                                 ngx_command_t *command,
                                                 void *conf) noexcept;

char *add_datadog_tag(ngx_conf_t *cf, ngx_array_t *tags, ngx_str_t key,
                      ngx_str_t value) noexcept;

char *set_datadog_tag(ngx_conf_t *cf, ngx_command_t *command,
                      void *conf) noexcept;

char *json_config_deprecated(ngx_conf_t *cf, ngx_command_t *command,
                             void *conf) noexcept;

char *toggle_opentracing(ngx_conf_t *cf, ngx_command_t *command,
                         void *conf) noexcept;

char *plugin_loading_deprecated(ngx_conf_t *cf, ngx_command_t *command,
                                void *conf) noexcept;

char *set_datadog_sample_rate(ngx_conf_t *cf, ngx_command_t *command,
                              void *conf) noexcept;

char *set_datadog_propagation_styles(ngx_conf_t *cf, ngx_command_t *command,
                                     void *conf) noexcept;

char *warn_deprecated_command_1_2_0(ngx_conf_t *cf, ngx_command_t *command,
                                    void *conf) noexcept;

constexpr datadog::nginx::directive tracing_directives[] = {
    {
        "opentracing",
        anywhere | NGX_CONF_TAKE1,
        toggle_opentracing,
        NGX_HTTP_LOC_CONF_OFFSET,
        0,
        nullptr,
    },

    {
        "datadog_tracing",
        anywhere | NGX_CONF_TAKE1,
        ngx_conf_set_flag_slot,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(datadog_loc_conf_t, enable_tracing),
        nullptr,
    },

    DEFINE_COMMAND_WITH_OLD_ALIAS(
        "datadog_trace_locations", "opentracing_trace_locations",
        anywhere | NGX_CONF_TAKE1, ngx_conf_set_flag_slot,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(datadog_loc_conf_t, enable_locations), nullptr),

    DEFINE_DEPRECATED_COMMAND_1_2_0("datadog_propagate_context",
                                    anywhere | NGX_CONF_NOARGS),

    DEFINE_DEPRECATED_COMMAND_1_2_0("opentracing_propagate_context",
                                    anywhere | NGX_CONF_NOARGS),

    DEFINE_DEPRECATED_COMMAND_1_2_0("opentracing_fastcgi_propagate_context",
                                    anywhere | NGX_CONF_NOARGS),

    DEFINE_DEPRECATED_COMMAND_1_2_0("datadog_fastcgi_propagate_context",
                                    anywhere | NGX_CONF_NOARGS),

    DEFINE_DEPRECATED_COMMAND_1_2_0("opentracing_grpc_propagate_context",
                                    anywhere | NGX_CONF_NOARGS),

    DEFINE_DEPRECATED_COMMAND_1_2_0("datadog_grpc_propagate_context",
                                    anywhere | NGX_CONF_NOARGS),

    DEFINE_COMMAND_WITH_OLD_ALIAS(
        "datadog_operation_name", "opentracing_operation_name",
        anywhere | NGX_CONF_TAKE1, ngx_http_set_complex_value_slot,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(datadog_loc_conf_t, operation_name_script), nullptr),

    DEFINE_COMMAND_WITH_OLD_ALIAS(
        "datadog_location_operation_name",
        "opentracing_location_operation_name", anywhere | NGX_CONF_TAKE1,
        ngx_http_set_complex_value_slot, NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(datadog_loc_conf_t, loc_operation_name_script), nullptr),

    {"datadog_resource_name", anywhere | NGX_CONF_TAKE1,
     ngx_http_set_complex_value_slot, NGX_HTTP_LOC_CONF_OFFSET,
     offsetof(datadog_loc_conf_t, resource_name_script), nullptr},

    {"datadog_location_resource_name", anywhere | NGX_CONF_TAKE1,
     ngx_http_set_complex_value_slot, NGX_HTTP_LOC_CONF_OFFSET,
     offsetof(datadog_loc_conf_t, loc_resource_name_script), nullptr},

    DEFINE_COMMAND_WITH_OLD_ALIAS(
        "datadog_trust_incoming_span", "opentracing_trust_incoming_span",
        anywhere | NGX_CONF_TAKE1, ngx_conf_set_flag_slot,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(datadog_loc_conf_t, trust_incoming_span), nullptr),

    DEFINE_COMMAND_WITH_OLD_ALIAS("datadog_tag", "opentracing_tag",
                                  anywhere | NGX_CONF_TAKE2, set_datadog_tag,
                                  NGX_HTTP_LOC_CONF_OFFSET, 0, nullptr),

    {"datadog_load_tracer",
     NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_CONF_TAKE2,
     plugin_loading_deprecated, NGX_HTTP_LOC_CONF_OFFSET, 0, nullptr},

    {"opentracing_load_tracer",
     NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_CONF_TAKE2,
     plugin_loading_deprecated, NGX_HTTP_LOC_CONF_OFFSET, 0, nullptr},

    {"datadog",
     NGX_MAIN_CONF | NGX_HTTP_MAIN_CONF | NGX_CONF_NOARGS | NGX_CONF_BLOCK,
     json_config_deprecated, NGX_HTTP_LOC_CONF_OFFSET, 0, nullptr},

    {"datadog_sample_rate",
     // NGX_CONF_TAKE12 means "take 1 or 2 args," not "take 12 args."
     NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF |
         NGX_CONF_TAKE12,
     set_datadog_sample_rate, NGX_HTTP_LOC_CONF_OFFSET, 0, nullptr},

    {"datadog_propagation_styles", NGX_HTTP_MAIN_CONF | NGX_CONF_1MORE,
     set_datadog_propagation_styles, NGX_HTTP_MAIN_CONF_OFFSET, 0, nullptr},
};

}  // namespace datadog::nginx
