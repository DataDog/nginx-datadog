#include "datadog_directive.h"

#include <cassert>

#include "datadog_conf_handler.h"

namespace datadog {
namespace nginx {

char *silently_ignore_command(ngx_conf_t *cf, ngx_command_t *command, void *) {
  ngx_conf_log_error(NGX_LOG_DEBUG, cf, 0, "Directive \"%V\" ignored",
                     &command->name);
  return NGX_OK;
}

char *alias_directive(ngx_conf_t *cf, ngx_command_t *command, void *) noexcept {
  if (command->post == nullptr) {
    return static_cast<char *>(NGX_CONF_ERROR);
  }

  auto src_directive = static_cast<char *>(command->post);

  const auto elements = static_cast<ngx_str_t *>(cf->args->elts);
  assert(cf->args->nelts >= 1);

  const ngx_str_t new_name_ngx{.len = strlen(src_directive),
                               .data = (u_char *)src_directive};
  ngx_conf_log_error(NGX_LOG_DEBUG, cf, 0, "Alias \"%V\" to \"%V\"",
                     &command->name, &new_name_ngx);

  // Rename the command and let `datadog_conf_handler` dispatch to the
  // appropriate handler.
  elements[0] = new_name_ngx;
  auto rcode = datadog_conf_handler({.conf = cf, .skip_this_module = false});
  if (rcode != NGX_OK) {
    return static_cast<char *>(NGX_CONF_ERROR);
  }

  return static_cast<char *>(NGX_CONF_OK);
}

char *warn_deprecated_command(ngx_conf_t *cf, ngx_command_t *command,
                              void *) noexcept {
  u_char buf[NGX_MAX_ERROR_STR] = {0};
  u_char *last = buf + NGX_MAX_ERROR_STR;
  u_char *p =
      ngx_slprintf(buf, last, "Directive \"%V\" is deprecated", &command->name);

  if (command->post != nullptr) {
    auto reason = static_cast<char *>(command->post);
    ngx_slprintf(p, last, ". %s", reason);
  }

  ngx_conf_log_error(NGX_LOG_WARN, cf, 0, "%s", buf);
  return NGX_OK;
}

char *err_deprecated_command(ngx_conf_t *cf, ngx_command_t *command,
                             void *) noexcept {
  u_char buf[NGX_MAX_ERROR_STR] = {0};
  u_char *last = buf + NGX_MAX_ERROR_STR;
  u_char *p =
      ngx_slprintf(buf, last, "Directive \"%V\" is deprecated", &command->name);

  if (command->post != nullptr) {
    auto reason = static_cast<char *>(command->post);
    ngx_slprintf(p, last, ". %s", reason);
  }

  ngx_conf_log_error(NGX_LOG_WARN, cf, 0, "%s", buf);
  return static_cast<char *>(NGX_CONF_ERROR);
}

}  // namespace nginx
}  // namespace datadog
