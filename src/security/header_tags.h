#pragma once

#include <datadog/span.h>
extern "C" {
#include <ngx_http.h>
}

namespace datadog::nginx::security {
void set_header_tags(bool has_attack, ngx_http_request_t &request,
                     ::datadog::tracing::Span &span);
}
