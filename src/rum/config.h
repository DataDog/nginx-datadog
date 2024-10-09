#pragma once

#include "datadog_conf.h"

extern "C" {
#include <nginx.h>
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>

// Handler for `datadog_rum_json_config` directive.
// Load a JSON RUM configuration file.
char *on_datadog_rum_json_config(ngx_conf_t *cf, ngx_command_t *command,
                                 void *conf);

// Handler for `datadog_rum_config` block directive.
// Parse the RUM configuration defined if the block.
char *on_datadog_rum_config(ngx_conf_t *cf, ngx_command_t *command, void *conf);

// Merge RUM location configurations.
char *datadog_rum_merge_loc_config(ngx_conf_t *cf,
                                   datadog::nginx::datadog_loc_conf_t *parent,
                                   datadog::nginx::datadog_loc_conf_t *child);

// clang-format off
#define DATADOG_RUM_DIRECTIVES \
    { \
      ngx_string("datadog_rum"), \
      NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_CONF_FLAG, \
      ngx_conf_set_flag_slot, NGX_HTTP_LOC_CONF_OFFSET, offsetof(datadog_loc_conf_t, rum_enable), NULL \
    }, \
    { \
      ngx_string("datadog_rum_config"), \
      NGX_HTTP_MAIN_CONF | NGX_HTTP_SRV_CONF | NGX_HTTP_LOC_CONF | NGX_CONF_BLOCK | NGX_CONF_TAKE1, \
      on_datadog_rum_config, NGX_HTTP_LOC_CONF_OFFSET, 0, NULL \
    },
// clang-format on
}
