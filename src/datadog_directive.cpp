#include "datadog_directive.h"

#include <cassert>

#include "datadog_conf_handler.h"

namespace datadog {
namespace nginx {

char *silently_ignore_command(ngx_conf_t *, ngx_command_t *, void *) {
  return NGX_OK;
}

char *alias_directive(ngx_conf_t *cf, ngx_command_t *command,
                      void *user_data) noexcept {
  if (user_data == nullptr) {
    return static_cast<char *>(NGX_CONF_ERROR);
  }

  auto src_directive = static_cast<std::string_view *>(user_data);

  const auto elements = static_cast<ngx_str_t *>(cf->args->elts);
  assert(cf->args->nelts >= 1);

  const ngx_str_t new_name_ngx = to_ngx_str(*src_directive);
  ngx_log_error(NGX_LOG_WARN, cf->log, 0,
                "Alias \"%V\" to \"%V\". Occurred at %V:%d", &elements[0],
                &new_name_ngx, &cf->conf_file->file.name, cf->conf_file->line);

  // Rename the command (opentracing_*  →  datadog_*) and let
  // `datadog_conf_handler` dispatch to the appropriate handler.
  elements[0] = new_name_ngx;
  auto rcode = datadog_conf_handler({.conf = cf, .skip_this_module = false});
  if (rcode != NGX_OK) {
    return static_cast<char *>(NGX_CONF_ERROR);
  }

  return static_cast<char *>(NGX_CONF_OK);
}

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
