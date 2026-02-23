#include "security/directives.h"

namespace datadog::nginx::security {

#ifdef WITH_WAF
char *waf_thread_pool_name(ngx_conf_t *cf, ngx_command_t *command,
                           void *conf) noexcept {
  datadog_loc_conf_t *loc_conf = static_cast<datadog_loc_conf_t *>(conf);
  ngx_str_t *value = static_cast<ngx_str_t *>(cf->args->elts);
  value++;  // 1st is the command name

  if (value->len == 0) {
    ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                       "datadog_waf_thread_pool_name cannot be empty");
    return static_cast<char *>(NGX_CONF_ERROR);
  }

  ngx_thread_pool_t *tp = ngx_thread_pool_get(cf->cycle, value);
  if (tp == nullptr) {
    ngx_conf_log_error(
        NGX_LOG_EMERG, cf, 0,
        "datadog_waf_thread_pool_name: \"%V\" not found. Either correct "
        "the name so it points to an existing thread pool or create a thread "
        "pool with such a name (using the 'thread_pool' directive)",
        value);
    return static_cast<char *>(NGX_CONF_ERROR);
  }

  loc_conf->waf_pool = tp;

  return NGX_CONF_OK;
}
#endif

}  // namespace datadog::nginx::security
