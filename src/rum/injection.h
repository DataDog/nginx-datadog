#pragma once

extern "C" {
#include <ngx_http.h>
}

#include <injectbrowsersdk.h>

#include "datadog_conf.h"

namespace datadog {
namespace nginx {
namespace rum {

class InjectionHandler final {
  enum state : char {
    init,
    searching,
    injected,
    error,
  } state_ = state::init;

  bool output_padding_;

  ngx_chain_t *busy_;
  ngx_chain_t *free_;

  Injector *injector_;

 public:
  InjectionHandler();
  ~InjectionHandler() = default;

  ngx_int_t on_rewrite_handler(ngx_http_request_t *r);

  ngx_int_t on_header_filter(
      ngx_http_request_t *r, datadog_loc_conf_t *cfg,
      ngx_http_output_header_filter_pt &next_header_filter);

  ngx_int_t on_body_filter(ngx_http_request_t *r, datadog_loc_conf_t *cfg,
                           ngx_chain_t *in,
                           ngx_http_output_body_filter_pt &next_body_filter);

 private:
  ngx_int_t output(ngx_http_request_t *r, ngx_chain_t *out,
                   ngx_http_output_body_filter_pt &next_body_filter);

  ngx_chain_t *inject(ngx_pool_t *pool, ngx_chain_t *in,
                      const BytesSlice *slices, uint32_t slices_length);
};

}  // namespace rum
}  // namespace nginx
}  // namespace datadog
