#include "discover_span_context_keys.h"
#include "ot.h"
#include "tracing_library.h"
#include "utility.h"

#include <algorithm>

namespace datadog {
namespace nginx {

ngx_array_t* discover_span_context_keys(ngx_pool_t* pool, ngx_log_t* log,
                                        const char* tracer_config_file) {
  std::string tracer_config;
  if (read_file(tracer_config_file, tracer_config)) {
    ngx_log_error(NGX_LOG_ERR, log, 0,
                  "failed to discover span context tags: unable to read configuration file: %s", tracer_config_file);
    return nullptr;
  }
  
  std::string error;
  const auto tag_names = TracingLibrary::span_tag_names(tracer_config, error);
  if (!error.empty()) {
    ngx_log_error(NGX_LOG_ERR, log, 0,
                  "failed to discover span context tags: %s",
                  error.c_str());
    return nullptr;
  }

  ngx_array_t* result = ngx_array_create(pool, tag_names.size(), sizeof(ot::string_view));
  if (result == nullptr) {
    throw std::bad_alloc{};
  }

  // For each `std::string` in `tag_names`, allocate a buffer from the `pool`,
  // copy the string data into it, and then push a new element onto `result`
  // that is an `ot::string_view` referring to the buffer.
  for (const std::string& tag_name : tag_names) {
    const auto buffer = static_cast<char*>(ngx_palloc(pool, tag_name.size()));
    if (buffer == nullptr) {
      throw std::bad_alloc{};
    }
    std::copy_n(tag_name.data(), tag_name.size(), buffer);

    const auto element = static_cast<ot::string_view*>(ngx_array_push(result));
    *element = ot::string_view(buffer, tag_name.size());
  }
  
  return result;
}

}  // namespace nginx
}  // namespace datadog
