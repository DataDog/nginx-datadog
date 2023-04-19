#pragma once

extern "C" {
#include <nginx.h>
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
}

namespace datadog {
namespace nginx {
char *propagate_datadog_context(ngx_conf_t *cf, ngx_command_t *command, void *conf) noexcept;

char *hijack_proxy_pass(ngx_conf_t *cf, ngx_command_t *command, void *conf) noexcept;

char *delegate_to_datadog_directive_with_warning(ngx_conf_t *cf, ngx_command_t *command,
                                                 void *conf) noexcept;

char *propagate_fastcgi_datadog_context(ngx_conf_t *cf, ngx_command_t *command,
                                        void *conf) noexcept;

char *hijack_fastcgi_pass(ngx_conf_t *cf, ngx_command_t *command, void *conf) noexcept;

char *propagate_grpc_datadog_context(ngx_conf_t *cf, ngx_command_t *command, void *conf) noexcept;

char *hijack_grpc_pass(ngx_conf_t *cf, ngx_command_t *command, void *conf) noexcept;

char *hijack_access_log(ngx_conf_t *cf, ngx_command_t *command, void *conf) noexcept;

char *add_datadog_tag(ngx_conf_t *cf, ngx_array_t *tags, ngx_str_t key, ngx_str_t value) noexcept;

char *set_datadog_tag(ngx_conf_t *cf, ngx_command_t *command, void *conf) noexcept;

char *json_config_deprecated(ngx_conf_t *cf, ngx_command_t *command, void *conf) noexcept;

char *set_datadog_operation_name(ngx_conf_t *cf, ngx_command_t *command, void *conf) noexcept;

char *set_datadog_location_operation_name(ngx_conf_t *cf, ngx_command_t *command,
                                          void *conf) noexcept;

char *set_datadog_resource_name(ngx_conf_t *cf, ngx_command_t *command, void *conf) noexcept;

char *set_datadog_location_resource_name(ngx_conf_t *cf, ngx_command_t *command,
                                         void *conf) noexcept;

char *toggle_opentracing(ngx_conf_t *cf, ngx_command_t *command, void *conf) noexcept;

char *datadog_enable(ngx_conf_t *cf, ngx_command_t *command, void *conf) noexcept;

char *datadog_disable(ngx_conf_t *cf, ngx_command_t *command, void *conf) noexcept;

char *plugin_loading_deprecated(ngx_conf_t *cf, ngx_command_t *command, void *conf) noexcept;

char *set_datadog_sample_rate(ngx_conf_t *cf, ngx_command_t *command, void *conf) noexcept;

char *set_datadog_propagation_styles(ngx_conf_t *cf, ngx_command_t *command, void *conf) noexcept;

}  // namespace nginx
}  // namespace datadog
