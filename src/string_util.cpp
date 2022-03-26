#include "string_util.h"

#include <algorithm>
#include <new>

namespace datadog {
namespace nginx {

ngx_str_t to_ngx_str(ngx_pool_t *pool, string_view s) {
  ngx_str_t result;
  result.data = static_cast<unsigned char *>(ngx_palloc(pool, s.size()));
  if (!result.data) {
    throw std::bad_alloc();
  }
  result.len = s.size();
  std::copy(s.begin(), s.end(), result.data);
  return result;
}

}  // namespace nginx
}  // namespace datadog
