#pragma once

extern "C" {
#include <nginx.h>
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
}

namespace datadog {
namespace nginx {
char *propagate_datadog_context(ngx_conf_t *cf, ngx_command_t *command,
                                    void *conf) noexcept;

char *hijack_proxy_pass(ngx_conf_t *cf, ngx_command_t *command,
                                    void *conf) noexcept;

char *delegate_to_datadog_directive_with_warning(ngx_conf_t *cf, ngx_command_t *command, void *conf) noexcept;

char *propagate_fastcgi_datadog_context(ngx_conf_t *cf,
                                            ngx_command_t *command,
                                            void *conf) noexcept;

char *hijack_fastcgi_pass(ngx_conf_t *cf, ngx_command_t *command,
                                    void *conf) noexcept;

char *propagate_grpc_datadog_context(ngx_conf_t *cf, ngx_command_t *command,
                                         void *conf) noexcept;

char *hijack_grpc_pass(ngx_conf_t *cf, ngx_command_t *command,
                                    void *conf) noexcept;

char *add_datadog_tag(ngx_conf_t *cf, ngx_array_t *tags, ngx_str_t key,
                          ngx_str_t value) noexcept;

char *set_datadog_tag(ngx_conf_t *cf, ngx_command_t *command,
                          void *conf) noexcept;

char *configure(ngx_conf_t *cf, ngx_command_t *command,
                          void *conf) noexcept;

char *set_datadog_operation_name(ngx_conf_t *cf, ngx_command_t *command,
                                     void *conf) noexcept;

char *set_datadog_location_operation_name(ngx_conf_t *cf,
                                              ngx_command_t *command,
                                              void *conf) noexcept;

char *set_tracer(ngx_conf_t *cf, ngx_command_t *command, void *conf) noexcept;

}  // namespace nginx
}  // namespace datadog
