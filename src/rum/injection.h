#pragma once

extern "C" {
#include <ngx_http.h>
}

#include <injectbrowsersdk.h>

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

  // Whether the response announces a Content-Length. If so, the injector pads
  // the response when it does not inject.
  bool has_content_length_;

  // Pointer to an Injector instance, used to scan and locate where the RUM
  // Browser SDK needs to be injected.
  Injector *injector_;

  // Writes `buf` to the injector and appends what it returns to the chain
  // ending at `tail`. Ends the injector on the last buffer. Sets `injected`
  // when the snippet goes in. Returns the new tail, or nullptr if out of
  // memory.
  ngx_chain_t **write_to_injector(ngx_pool_t *pool, ngx_chain_t **tail,
                                  ngx_buf_t *buf, bool &injected);
  void report_injected(ngx_http_request_t *r, datadog_loc_conf_t *cfg);
  void report_failed(ngx_http_request_t *r, datadog_loc_conf_t *cfg);

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

  // Handles the logging phase of an HTTP request.
  // @param r - HTTP request being processed.
  // @return ngx_int_t - Status code indicating success or failure.
  ngx_int_t on_log_request(ngx_http_request_t *r);
};

}  // namespace rum
}  // namespace nginx
}  // namespace datadog
