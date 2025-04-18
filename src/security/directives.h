#pragma once

#include "common/directives.h"
#include "datadog_conf.h"
#include "datadog_directive.h"

extern "C" {
#include <ngx_core.h>
#include <ngx_thread_pool.h>
}

namespace datadog::nginx::security {

#ifdef WITH_WAF
char *waf_thread_pool_name(ngx_conf_t *cf, ngx_command_t *command,
                           void *conf) noexcept;

constexpr directive appsec_directives[] = {
    {"datadog_waf_thread_pool_name",
     NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF |
         NGX_CONF_TAKE1,
     waf_thread_pool_name, NGX_HTTP_LOC_CONF_OFFSET,
     offsetof(datadog_loc_conf_t, waf_pool), NULL},

    {
        "datadog_appsec_enabled",
        NGX_HTTP_MAIN_CONF | NGX_CONF_TAKE1,
        ngx_conf_set_flag_slot,
        NGX_HTTP_MAIN_CONF_OFFSET,
        offsetof(datadog_main_conf_t, appsec_enabled),
        nullptr,
    },

    {
        "datadog_appsec_ruleset_file",
        NGX_HTTP_MAIN_CONF | NGX_CONF_TAKE1,
        ngx_conf_set_str_slot,
        NGX_HTTP_MAIN_CONF_OFFSET,
        offsetof(datadog_main_conf_t, appsec_ruleset_file),
        &datadog::common::ngx_conf_post_file_exists,
    },

    {
        "datadog_appsec_http_blocked_template_json",
        NGX_HTTP_MAIN_CONF | NGX_CONF_TAKE1,
        ngx_conf_set_str_slot,
        NGX_HTTP_MAIN_CONF_OFFSET,
        offsetof(datadog_main_conf_t, appsec_http_blocked_template_json),
        &datadog::common::ngx_conf_post_file_exists,
    },

    {
        "datadog_appsec_http_blocked_template_html",
        NGX_HTTP_MAIN_CONF | NGX_CONF_TAKE1,
        ngx_conf_set_str_slot,
        NGX_HTTP_MAIN_CONF_OFFSET,
        offsetof(datadog_main_conf_t, appsec_http_blocked_template_html),
        &datadog::common::ngx_conf_post_file_exists,
    },

    {
        "datadog_client_ip_header",
        NGX_HTTP_MAIN_CONF | NGX_CONF_TAKE1,  // TODO allow it more fine-grained
        ngx_conf_set_str_slot,
        NGX_HTTP_MAIN_CONF_OFFSET,
        offsetof(datadog_main_conf_t, custom_client_ip_header),
        nullptr,
    },

    {
        "datadog_appsec_waf_timeout",
        NGX_HTTP_MAIN_CONF | NGX_CONF_TAKE1,
        ngx_conf_set_msec_slot,
        NGX_HTTP_MAIN_CONF_OFFSET,
        offsetof(datadog_main_conf_t, appsec_waf_timeout_ms),
        nullptr,
    },

    {
        "datadog_appsec_obfuscation_key_regex",
        NGX_HTTP_MAIN_CONF | NGX_CONF_TAKE1,
        ngx_conf_set_str_slot,
        NGX_HTTP_MAIN_CONF_OFFSET,
        offsetof(datadog_main_conf_t, appsec_obfuscation_key_regex),
        nullptr,
    },

    {
        "datadog_appsec_obfuscation_value_regex",
        NGX_HTTP_MAIN_CONF | NGX_CONF_TAKE1,
        ngx_conf_set_str_slot,
        NGX_HTTP_MAIN_CONF_OFFSET,
        offsetof(datadog_main_conf_t, appsec_obfuscation_value_regex),
        nullptr,
    },

    {
        "datadog_appsec_max_saved_output_data",
        NGX_HTTP_MAIN_CONF | NGX_CONF_TAKE1,
        ngx_conf_set_size_slot,
        NGX_HTTP_MAIN_CONF_OFFSET,
        offsetof(datadog_main_conf_t, appsec_max_saved_output_data),
        nullptr,
    },
};
#endif

}  // namespace datadog::nginx::security
