#include "datadog_directive.h"

#include <cassert>

namespace datadog {
namespace nginx {

char *warn_deprecated_command_datadog_tracing(ngx_conf_t *cf,
                                              ngx_command_t * /*command*/,
                                              void * /*conf*/) noexcept {
  const auto elements = static_cast<ngx_str_t *>(cf->args->elts);
  assert(cf->args->nelts >= 1);

  ngx_log_error(
      NGX_LOG_WARN, cf->log, 0,
      "Directive \"%V\" is deprecated. Use datadog_tracing on/off instead",
      &elements[0]);

  return NGX_OK;
}

}  // namespace nginx
}  // namespace datadog
