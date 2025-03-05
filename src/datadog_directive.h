#pragma once

extern "C" {
#include <nginx.h>
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
}

namespace datadog {
namespace nginx {

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

char *set_datadog_agent_url(ngx_conf_t *, ngx_command_t *, void *conf) noexcept;

char *warn_deprecated_command_1_2_0(ngx_conf_t *cf, ngx_command_t * /*command*/,
                                    void * /*conf*/) noexcept;

char *warn_deprecated_command_datadog_tracing(ngx_conf_t *cf,
                                              ngx_command_t * /*command*/,
                                              void * /*conf*/) noexcept;

#ifdef WITH_WAF
char *waf_thread_pool_name(ngx_conf_t *cf, ngx_command_t *command,
                           void *conf) noexcept;
#endif

}  // namespace nginx
}  // namespace datadog
