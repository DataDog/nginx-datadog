#include "discover_span_context_keys.h"
#include "ot.h"
#include "tracing_library.h"
#include "utility.h"

#include <algorithm>

namespace datadog {
namespace nginx {

ngx_array_t* discover_span_context_keys(ngx_pool_t* pool, ngx_log_t* log,
                                        ot::string_view tracer_config) {
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

  for (const ot::string_view& tag_name : tag_names) {
    const auto new_element = static_cast<ot::string_view*>(ngx_array_push(result));
    *new_element = tag_name;
  }
  
  return result;
}

}  // namespace nginx
}  // namespace datadog
