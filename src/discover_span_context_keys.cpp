#include "discover_span_context_keys.h"
#include "ot.h"

#include "load_tracer.h"

#include <opentracing/ext/tags.h>
#include <opentracing/propagation.h>
#include <algorithm>
#include <iostream>
#include <new>

namespace datadog {
namespace nginx {
//------------------------------------------------------------------------------
// HeaderKeyWriter
//------------------------------------------------------------------------------
namespace {
class HeaderKeyWriter : public ot::HTTPHeadersWriter {
 public:
  HeaderKeyWriter(ngx_pool_t* pool, std::vector<ot::string_view>& keys)
      : pool_{pool}, keys_(keys) {}

  ot::expected<void> Set(
      ot::string_view key,
      ot::string_view value) const override {
    auto data = static_cast<char*>(ngx_palloc(pool_, key.size()));
    if (data == nullptr) {
      throw std::bad_alloc{};
    }
    std::copy_n(key.data(), key.size(), data);

    keys_.emplace_back(data, key.size());

    return {};
  }

 private:
  ngx_pool_t* pool_;
  std::vector<ot::string_view>& keys_;
};
}  // namespace

//------------------------------------------------------------------------------
// discover_span_context_keys
//------------------------------------------------------------------------------
// Loads the vendor tracing library and creates a dummy span so as to obtain
// a list of the keys used for span context propagation. This is necessary to
// make context propagation work for requests proxied upstream.
//
// See propagate_datadog_context, set_tracer.
//
// Note: Any keys that a tracer might use for propagation that aren't discovered
// here will get dropped during propagation.
ngx_array_t* discover_span_context_keys(ngx_pool_t* pool, ngx_log_t* log,
                                        const char* tracing_library,
                                        const char* tracer_config_file) {
  ot::DynamicTracingLibraryHandle dummy_handle;
  std::shared_ptr<ot::Tracer> tracer;
  auto rcode =
      load_tracer(log, tracing_library, tracer_config_file, dummy_handle, tracer);
  if (rcode != NGX_OK) {
    return nullptr;
  }
  auto span = tracer->StartSpan("dummySpan");
  span->SetTag(ot::ext::sampling_priority, 0);
  std::vector<ot::string_view> keys;
  HeaderKeyWriter carrier_writer{pool, keys};
  auto was_successful = tracer->Inject(span->context(), carrier_writer);
  if (!was_successful) {
    ngx_log_error(NGX_LOG_ERR, log, 0,
                  "failed to discover span context tags: %s",
                  was_successful.error().message().c_str());
    return nullptr;
  }
  ngx_array_t* result =
      ngx_array_create(pool, keys.size(), sizeof(ot::string_view));
  if (result == nullptr) {
    throw std::bad_alloc{};
  }

  for (auto key : keys) {
    auto element =
        static_cast<ot::string_view*>(ngx_array_push(result));
    *element = key;
  }
  return result;
}
}  // namespace nginx
}  // namespace datadog
