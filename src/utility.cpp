#include "utility.h"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <string>

namespace datadog {
namespace nginx {

ngx_str_t to_ngx_str(ngx_pool_t *pool, const std::string &s) {
  return to_ngx_str(ot::string_view(s));
}

ngx_str_t to_ngx_str(ngx_pool_t *pool, ot::string_view s) {
  ngx_str_t result;
  result.data = static_cast<unsigned char *>(ngx_palloc(pool, s.size()));
  if (!result.data) return {0, nullptr};
  result.len = s.size();
  std::copy(s.begin(), s.end(), result.data);
  return result;
}

std::chrono::system_clock::time_point to_system_timestamp(
    time_t epoch_seconds, ngx_msec_t epoch_milliseconds) {
  auto epoch_duration = std::chrono::seconds{epoch_seconds} +
                        std::chrono::milliseconds{epoch_milliseconds};
  return std::chrono::system_clock::from_time_t(std::time_t{0}) +
         epoch_duration;
}

int read_file(const char* path, std::string& destination) {
  std::filebuf file;
  if (file.open(path, std::ios::in) == nullptr) {
    return 1;
  }

  destination.append(std::istreambuf_iterator<char>{&file}, std::istreambuf_iterator<char>{});
  return 0;
}

}  // namespace nginx
}  // namespace datadog
