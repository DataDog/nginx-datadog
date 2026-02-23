#pragma once

extern "C" {
#include <nginx.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <ngx_http_core_module.h>
}

namespace datadog {
namespace nginx {

extern ngx_http_output_header_filter_pt ngx_http_next_header_filter;
extern ngx_http_output_body_filter_pt ngx_http_next_output_body_filter;

ngx_int_t on_enter_block(ngx_http_request_t* request) noexcept;
#ifdef WITH_WAF
ngx_int_t on_access(ngx_http_request_t* request) noexcept;
#endif
ngx_int_t on_log_request(ngx_http_request_t* request) noexcept;

ngx_int_t on_header_filter(ngx_http_request_t* r) noexcept;

#ifdef WITH_WAF
extern ngx_http_request_body_filter_pt ngx_http_next_request_body_filter;
ngx_int_t request_body_filter(ngx_http_request_t* r,
                              ngx_chain_t* chain) noexcept;
#endif

ngx_int_t on_output_body_filter(ngx_http_request_t* r,
                                ngx_chain_t* chain) noexcept;

ngx_int_t on_precontent_phase(ngx_http_request_t* request) noexcept;

}  // namespace nginx
}  // namespace datadog
