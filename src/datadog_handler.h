#pragma once

extern "C" {
#include <nginx.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <ngx_http_core_module.h>
}

namespace datadog {
namespace nginx {

ngx_int_t on_enter_block(ngx_http_request_t *request) noexcept;
ngx_int_t on_access(ngx_http_request_t *request) noexcept;
ngx_int_t on_log_request(ngx_http_request_t *request) noexcept;

extern ngx_http_output_header_filter_pt ngx_http_next_output_header_filter;
ngx_int_t output_header_filter(ngx_http_request_t *r) noexcept;
}  // namespace nginx
}  // namespace datadog
