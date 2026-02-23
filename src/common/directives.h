#pragma once

extern "C" {
#include <ngx_core.h>
}

namespace datadog::common {

/// Checks if the file specified in the configuration exists.
///
/// This function is typically used as a post-processing callback for NGINX
/// configuration It verifies that the file specified in a configuration
/// directive actually exists on the filesystem.
char* check_file_exists(ngx_conf_t* cf, void* post, void* data);

#ifdef WITH_WAF
/// Post handler for checking a filepath exists.
static ngx_conf_post_t ngx_conf_post_file_exists = {check_file_exists};
#endif

}  // namespace datadog::common
