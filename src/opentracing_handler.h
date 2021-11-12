#pragma once

extern "C" {
#include <nginx.h>
#include <ngx_core.h>
#include <ngx_http.h>
}

namespace datadog {
namespace nginx {
//------------------------------------------------------------------------------
// on_enter_block
//------------------------------------------------------------------------------
ngx_int_t on_enter_block(ngx_http_request_t *request) noexcept;

//------------------------------------------------------------------------------
// on_log_request
//------------------------------------------------------------------------------
ngx_int_t on_log_request(ngx_http_request_t *request) noexcept;
}  // namespace nginx
}  // namespace datadog
