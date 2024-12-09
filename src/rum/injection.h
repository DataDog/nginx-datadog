#pragma once

extern "C" {
#include <ngx_http.h>
}

#include <injectbrowsersdk.h>

#include <span>

#include "datadog_conf.h"

namespace datadog {
namespace nginx {
namespace rum {

// Class is responsible for managing the injection of the RUM Browser SDK
// into HTML responses. It handles the various stages of processing a request,
// from filtering HTTP responses to injecting the RUM Browser SDK.
//
// The class operates based on an internal state machine due to the way NGINX
// proccess requests.
class InjectionHandler final {
  // Enum representing the states of the injection process.
  enum state : char {
    init,
    searching,
    injected,
    error,
    failed,  ///< no injection point found
  } state_ = state::init;

  // A flag indicating whether padding should be added to the HTML responses.
  bool output_padding_;
  bool rum_first_csp_ = true;

  // Pointer to an Injector instance, used to scan and locate where the RUM
  // Browser SDK needs to be injected.
  Injector *injector_;

 public:
  InjectionHandler();
  ~InjectionHandler();

  // Handles the rewrite phase of an HTTP request (NGX_HTTP_REWRITE_PHASE)
  // @param r - HTTP request being processed.
  // @return ngx_int_t - Status code indicating success or failure.
  ngx_int_t on_rewrite_handler(ngx_http_request_t *r);

  // Handles the header filtering phase of an HTTP request.
  // @param r - HTTP request being processed.
  // @param cfg - Location configuration of the module.
  // @param next_header_filter - Reference to the next header filter in the
  // NGINX filter chain.
  // @return ngx_int_t - Status code indicating success or failure.
  ngx_int_t on_header_filter(
      ngx_http_request_t *r, datadog_loc_conf_t *cfg,
      ngx_http_output_header_filter_pt &next_header_filter);

  // Handles the body modification of an HTTP request.
  // @param r - HTTP request being processed.
  // @param cfg - Location configuration of the module.
  // @param in - Incoming chain of buffers containing the response body.
  // @param next_body_filter - Reference to the next body filter in the NGINX
  // body filter chain.
  // @return ngx_int_t - Status code indicating success or failure.
  ngx_int_t on_body_filter(ngx_http_request_t *r, datadog_loc_conf_t *cfg,
                           ngx_chain_t *in,
                           ngx_http_output_body_filter_pt &next_body_filter);

 private:
  // Sends the output to the next body filter.
  // @param r - HTTP request being processed.
  // @param out - Chain of buffers containing the response to send.
  // @param next_body_filter - Reference to the next body filter in the NGINX
  // body filter chain.
  ngx_int_t output(ngx_http_request_t *r, ngx_chain_t *out,
                   ngx_http_output_body_filter_pt &next_body_filter);

  // Injects the RUM Browser SDK into a chain of buffers at the appropriate
  // location.
  // @param pool - Memory pool used for allocation.
  // @param in - Incoming chain of buffers containing the response body.
  // @param slices - Pointer to an array of `BytesSlice`.
  // @param slices_length - Number of elements in the `slices` array.
  // @return ngx_chain_t* - Chain of buffers with the injected data.
  ngx_chain_t *inject(ngx_pool_t *pool, ngx_chain_t *in,
                      std::span<const BytesSlice> slices);
};

}  // namespace rum
}  // namespace nginx
}  // namespace datadog
