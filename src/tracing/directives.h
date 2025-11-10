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

char *set_datadog_tag(ngx_conf_t *cf, ngx_command_t *command,
                      void *conf) noexcept;

char *set_datadog_baggage_tags(ngx_conf_t *cf, ngx_command_t *command,
                               void *conf) noexcept;

char *set_datadog_sample_rate(ngx_conf_t *cf, ngx_command_t *command,
                              void *conf) noexcept;

char *set_datadog_propagation_styles(ngx_conf_t *cf, ngx_command_t *command,
                                     void *conf) noexcept;

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winvalid-offsetof"
constexpr datadog::nginx::directive tracing_directives[] = {
    {
        "datadog_tracing",
        anywhere | NGX_CONF_TAKE1,
        ngx_conf_set_flag_slot,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(datadog_loc_conf_t, enable_tracing),
        nullptr,
    },
    {
        "datadog_apm_tracing_enabled",
        NGX_HTTP_MAIN_CONF | NGX_CONF_TAKE1,
        ngx_conf_set_flag_slot,
        NGX_HTTP_MAIN_CONF_OFFSET,
        offsetof(datadog_main_conf_t, apm_tracing_enabled),
        nullptr,
    },
    {
        "datadog_trace_locations",
        anywhere | NGX_CONF_TAKE1,
        ngx_conf_set_flag_slot,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(datadog_loc_conf_t, enable_locations),
        nullptr,
    },

    {
        "datadog_operation_name",
        anywhere | NGX_CONF_TAKE1,
        ngx_http_set_complex_value_slot,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(datadog_loc_conf_t, operation_name_script),
        nullptr,
    },

    {
        "datadog_location_operation_name",
        anywhere | NGX_CONF_TAKE1,
        ngx_http_set_complex_value_slot,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(datadog_loc_conf_t, loc_operation_name_script),
        nullptr,
    },

    {
        "datadog_resource_name",
        anywhere | NGX_CONF_TAKE1,
        ngx_http_set_complex_value_slot,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(datadog_loc_conf_t, resource_name_script),
        nullptr,
    },

    {
        "datadog_location_resource_name",
        anywhere | NGX_CONF_TAKE1,
        ngx_http_set_complex_value_slot,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(datadog_loc_conf_t, loc_resource_name_script),
        nullptr,
    },

    {
        "datadog_trust_incoming_span",
        anywhere | NGX_CONF_TAKE1,
        ngx_conf_set_flag_slot,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(datadog_loc_conf_t, trust_incoming_span),
        nullptr,
    },

    {
        "datadog_tag",
        anywhere | NGX_CONF_TAKE2,
        set_datadog_tag,
        NGX_HTTP_LOC_CONF_OFFSET,
        0,
        nullptr,
    },

    {
        "datadog_sample_rate",
        // NGX_CONF_TAKE12 means "take 1 or 2 args," not "take 12 args."
        NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF |
            NGX_CONF_TAKE12,
        set_datadog_sample_rate,
        NGX_HTTP_LOC_CONF_OFFSET,
        0,
        nullptr,
    },

    {
        "datadog_propagation_styles",
        NGX_HTTP_MAIN_CONF | NGX_CONF_1MORE,
        set_datadog_propagation_styles,
        NGX_HTTP_MAIN_CONF_OFFSET,
        0,
        nullptr,
    },

    {
        "datadog_baggage_tags_enabled",
        anywhere | NGX_CONF_TAKE1,
        ngx_conf_set_flag_slot,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(datadog_loc_conf_t, baggage_span_tags_enabled),
        nullptr,
    },

    {
        "datadog_baggage_tags_keys",
        anywhere | NGX_CONF_1MORE,
        set_datadog_baggage_tags,
        NGX_HTTP_LOC_CONF_OFFSET,
        0,
        nullptr,
    },

    {
        "datadog_resource_renaming_enabled",
        NGX_HTTP_MAIN_CONF | NGX_CONF_TAKE1,
        ngx_conf_set_flag_slot,
        NGX_HTTP_MAIN_CONF_OFFSET,
        offsetof(datadog_main_conf_t, resource_renaming_enabled),
        nullptr,
    },

    {
        "datadog_resource_renaming_always_simplified_endpoint",
        NGX_HTTP_MAIN_CONF | NGX_CONF_TAKE1,
        ngx_conf_set_flag_slot,
        NGX_HTTP_MAIN_CONF_OFFSET,
        offsetof(datadog_main_conf_t,
                 resource_renaming_always_simplified_endpoint),
        nullptr,
    },

    // aliases opentracing (legacy)
    ALIAS_COMMAND("datadog_tracing", "opentracing", anywhere | NGX_CONF_TAKE1),
    ALIAS_COMMAND("datadog_operation_name", "opentracing_operation_name",
                  anywhere | NGX_CONF_TAKE1),
    ALIAS_COMMAND("datadog_location_operation_name",
                  "opentracing_location_operation_name",
                  anywhere | NGX_CONF_TAKE1),
    ALIAS_COMMAND("datadog_trust_incoming_span",
                  "opentracing_trust_incoming_span", anywhere | NGX_CONF_TAKE1),
    ALIAS_COMMAND("datadog_tag", "opentracing_tag", anywhere | NGX_CONF_TAKE2),
    ALIAS_COMMAND("datadog_trace_locations", "opentracing_trace_locations",
                  anywhere | NGX_CONF_TAKE1),

    // aliases opentelemetry. From:
    // <https://github.com/open-telemetry/opentelemetry-cpp-contrib/blob/3d2bf3afb465e3cc6c5ff12e4f4cf8f86fd42943/instrumentation/nginx/src/otel_ngx_module.cpp#L1140>
    ALIAS_COMMAND("datadog_operation_name", "opentelemetry_operation_name",
                  anywhere | NGX_CONF_TAKE1),
    ALIAS_COMMAND("datadog_trust_incoming_span",
                  "opentelemetry_trust_incoming_spans",
                  anywhere | NGX_CONF_TAKE1),
    ALIAS_COMMAND("datadog_sample_rate", "opentelemetry_traces_sampler_ratio",
                  anywhere | NGX_CONF_TAKE1),
    ALIAS_COMMAND("datadog_tracing", "opentelemetry",
                  anywhere | NGX_CONF_TAKE1),
    ALIAS_COMMAND("datadog_tag", "opentelemetry_attribute",
                  anywhere | NGX_CONF_TAKE2),
    IGNORE_COMMAND("opentelemetry_propagate",
                   anywhere | NGX_CONF_NOARGS | NGX_CONF_TAKE1),
    IGNORE_COMMAND("opentelemetry_capture_headers", anywhere | NGX_CONF_TAKE1),
    IGNORE_COMMAND("opentelemetry_span_processor", anywhere | NGX_CONF_TAKE1),
    IGNORE_COMMAND("opentelemetry_bsp_max_queue_size",
                   anywhere | NGX_CONF_TAKE1),
    IGNORE_COMMAND("opentelemetry_bsp_schedule_delay_millis",
                   anywhere | NGX_CONF_TAKE1),
    IGNORE_COMMAND("opentelemetry_bsp_max_export_batch_size",
                   anywhere | NGX_CONF_TAKE1),
    IGNORE_COMMAND("opentelemetry_traces_sampler", anywhere | NGX_CONF_TAKE1),
    IGNORE_COMMAND("opentelemetry_sensitive_header_names",
                   anywhere | NGX_CONF_TAKE1),
    IGNORE_COMMAND("opentelemetry_sensitive_header_values",
                   anywhere | NGX_CONF_TAKE1),
    IGNORE_COMMAND("opentelemetry_ignore_paths", anywhere | NGX_CONF_TAKE1),

    // deprecated
    ERROR_DEPRECATED_COMMAND(
        "datadog",
        NGX_MAIN_CONF | NGX_HTTP_MAIN_CONF | NGX_CONF_NOARGS | NGX_CONF_BLOCK,
        "The datadog { ... } block directive is no longer supported. "
        "Use the specific datadog_* "
        "directives instead, or use DD_TRACE_* environment variables"),

    ERROR_DEPRECATED_COMMAND(
        "datadog_load_tracer",
        NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_CONF_TAKE2,
        "Use the separate datadog_* directives to configure tracing"),

    ERROR_DEPRECATED_COMMAND(
        "opentracing_load_tracer",
        NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_CONF_TAKE2,
        "Use the separate datadog_* directives to configure tracing"),

    WARN_DEPRECATED_COMMAND("datadog_propagate_context",
                            anywhere | NGX_CONF_NOARGS, nullptr),

    WARN_DEPRECATED_COMMAND("opentracing_propagate_context",
                            anywhere | NGX_CONF_NOARGS, nullptr),

    WARN_DEPRECATED_COMMAND("opentracing_fastcgi_propagate_context",
                            anywhere | NGX_CONF_NOARGS, nullptr),

    WARN_DEPRECATED_COMMAND("datadog_fastcgi_propagate_context",
                            anywhere | NGX_CONF_NOARGS, nullptr),

    WARN_DEPRECATED_COMMAND("opentracing_grpc_propagate_context",
                            anywhere | NGX_CONF_NOARGS, nullptr),

    WARN_DEPRECATED_COMMAND("datadog_grpc_propagate_context",
                            anywhere | NGX_CONF_NOARGS, nullptr),
};
#pragma clang diagnostic pop

}  // namespace datadog::nginx
