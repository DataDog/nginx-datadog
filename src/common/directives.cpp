#include "common/directives.h"

#include <cassert>
#include <filesystem>

#include "string_util.h"

namespace datadog::common {

char *check_file_exists(ngx_conf_t *cf, void *post, void *data) {
  assert(data != nullptr);
  ngx_str_t *s = (ngx_str_t *)data;
  if (!std::filesystem::exists(nginx::to_string_view(*s))) {
    ngx_conf_log_error(NGX_LOG_ERR, cf, 0, "Failed to open file: \"%V\"", s);
    return static_cast<char *>(NGX_CONF_ERROR);
  }
  return NGX_CONF_OK;
}

}  // namespace datadog::common
