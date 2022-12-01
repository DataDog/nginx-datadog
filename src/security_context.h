#pragma once

#include "security_library.h"

#include <opentracing/tracer.h>
#include "ot.h"

extern "C" {
#include <nginx.h>
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
}

namespace datadog {
namespace nginx {

class security_context {
public:
    security_context();
    ~security_context();

    void on_log_request(ngx_http_request_t* request, ot::Span &span);
protected:
    ddwaf_context ctx_{nullptr};
};

}
}
