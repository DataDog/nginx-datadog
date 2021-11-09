#include <opentracing/propagation.h>
#include "ot.h"

#include <opentracing/tracer.h>
#include "utility.h"
using ot::expected;
using ot::make_unexpected;
using ot::string_view;

extern "C" {
#include <nginx.h>
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
}

namespace datadog {
namespace nginx {
//------------------------------------------------------------------------------
// NgxHeaderCarrierReader
//------------------------------------------------------------------------------
namespace {
class NgxHeaderCarrierReader : public ot::HTTPHeadersReader {
 public:
  explicit NgxHeaderCarrierReader(const ngx_http_request_t *request)
      : request_{request} {}

  expected<void> ForeachKey(
      std::function<expected<void>(string_view, string_view)> f)
      const override {
    expected<void> result;
    for_each<ngx_table_elt_t>(
        request_->headers_in.headers, [&](const ngx_table_elt_t &header) {
          if (!result) return;
          auto key = string_view{reinterpret_cast<char *>(header.lowcase_key),
                                 header.key.len};
          auto value = string_view{reinterpret_cast<char *>(header.value.data),
                                   header.value.len};
          result = f(key, value);
        });
    return result;
  }

 private:
  const ngx_http_request_t *request_;
};
}  // namespace

//------------------------------------------------------------------------------
// extract_span_context
//------------------------------------------------------------------------------
std::unique_ptr<ot::SpanContext> extract_span_context(
    const ot::Tracer &tracer, const ngx_http_request_t *request) {
  auto carrier_reader = NgxHeaderCarrierReader{request};
  auto span_context_maybe = tracer.Extract(carrier_reader);
  if (!span_context_maybe) {
    ngx_log_error(
        NGX_LOG_ERR, request->connection->log, 0,
        "failed to extract an opentracing span context from request %p: %s",
        request, span_context_maybe.error().message().c_str());
    return nullptr;
  }
  ngx_log_debug1(NGX_LOG_DEBUG_HTTP, request->connection->log, 0,
                 "extraced opentracing span context from request %p", request);
  return std::move(*span_context_maybe);
}
}  // namespace nginx
}  // namespace datadog
