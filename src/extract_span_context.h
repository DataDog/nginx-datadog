#pragma once

#include <opentracing/tracer.h>
#include <opentracing/span.h>
#include "ot.h"

#include <memory>

extern "C" {
#include <ngx_http.h>
}

namespace datadog {
namespace nginx {

// Use the specified `tracer` to extract tracing context from the client-sent
// headers of the specified `request`, and return the corresponding
// `SpanContext`, or `nullptr` if there is no tracing context to extract.
std::unique_ptr<ot::SpanContext> extract_span_context(
    const ot::Tracer &tracer, const ngx_http_request_t *request);

}  // namespace nginx
}  // namespace datadog
