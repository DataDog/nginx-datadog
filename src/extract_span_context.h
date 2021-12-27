#pragma once

// TODO: document

#include <opentracing/tracer.h>
#include <opentracing/span.h>
#include "ot.h"

#include <memory>

extern "C" {
#include <ngx_http.h>
}

namespace datadog {
namespace nginx {

std::unique_ptr<ot::SpanContext> extract_span_context(
    const ot::Tracer &tracer, const ngx_http_request_t *request);

}  // namespace nginx
}  // namespace datadog
