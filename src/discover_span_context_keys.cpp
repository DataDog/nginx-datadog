#include "discover_span_context_keys.h"

#include <algorithm>

#include "string_util.h"
#include "tracing_library.h"

namespace datadog {
namespace nginx {

ngx_array_t* discover_span_context_keys(ngx_pool_t* pool, ngx_log_t* log,
                                        std::string_view tracer_config) {
  std::string error;
  const auto tag_names =
      TracingLibrary::propagation_header_names(tracer_config, error);
  if (!error.empty()) {
    ngx_log_error(NGX_LOG_ERR, log, 0,
                  "failed to discover span context tags: %s", error.c_str());
    return nullptr;
  }

  ngx_array_t* result =
      ngx_array_create(pool, tag_names.size(), sizeof(std::string_view));
  if (result == nullptr) {
    throw std::bad_alloc{};
  }

  for (const std::string_view& tag_name : tag_names) {
    const auto new_element =
        static_cast<std::string_view*>(ngx_array_push(result));
    *new_element = tag_name;
  }

  return result;
}

}  // namespace nginx
}  // namespace datadog
