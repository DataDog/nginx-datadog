#include "header_tags.h"

#include <cassert>
#include <charconv>
#include <string_view>

#include "../dd.h"
#include "string_util.h"
#include "util.h"

using namespace std::literals;
namespace dnsec = datadog::nginx::security;
using datadog::nginx::to_string_view;

namespace {
void each_req_header(bool has_attack, const ngx_table_elt_t& h,
                     dd::Span& span) {
  switch (h.hash) {
#define CASE_REQ_HEADER_ATTACK(header)                                         \
  case dnsec::ngx_hash_ce(""sv header):                                        \
    if (has_attack && dnsec::req_key_equals_ci(h, ""sv header)) {              \
      span.set_tag("http.request.headers."sv header, to_string_view(h.value)); \
    }                                                                          \
    break;

    // request headers only set when there is an attack
    CASE_REQ_HEADER_ATTACK("x-forwarded-for")
    CASE_REQ_HEADER_ATTACK("x-real-ip")
    CASE_REQ_HEADER_ATTACK("true-client-ip")
    CASE_REQ_HEADER_ATTACK("x-client-ip")
    CASE_REQ_HEADER_ATTACK("x-forwarded")
    CASE_REQ_HEADER_ATTACK("forwarded-for")
    CASE_REQ_HEADER_ATTACK("x-cluster-client-ip")
    CASE_REQ_HEADER_ATTACK("fastly-client-ip")
    CASE_REQ_HEADER_ATTACK("cf-connecting-ip")
    CASE_REQ_HEADER_ATTACK("cf-connecting-ipv6")
    CASE_REQ_HEADER_ATTACK("forwarded")
    CASE_REQ_HEADER_ATTACK("via")
    CASE_REQ_HEADER_ATTACK("content-length")
    CASE_REQ_HEADER_ATTACK("content-encoding")
    CASE_REQ_HEADER_ATTACK("content-language")
    CASE_REQ_HEADER_ATTACK("host")
    CASE_REQ_HEADER_ATTACK("accept-encoding")
    CASE_REQ_HEADER_ATTACK("accept-language")

#define CASE_REQ_HEADER_APPSEC_ENABLED(header)                                 \
  case dnsec::ngx_hash_ce(""sv header):                                        \
    if (dnsec::resp_key_equals_ci(h, ""sv header)) {                           \
      span.set_tag("http.request.headers."sv header, to_string_view(h.value)); \
    }                                                                          \
    break;

    // request headers set unconditionally when appsec is enabled
    CASE_REQ_HEADER_APPSEC_ENABLED("content-type")
    CASE_REQ_HEADER_APPSEC_ENABLED("user-agent")
    CASE_REQ_HEADER_APPSEC_ENABLED("accept")
    CASE_REQ_HEADER_APPSEC_ENABLED("x-amzn-trace-id")
    CASE_REQ_HEADER_APPSEC_ENABLED("cloudfront-viewer-ja3-fingerprint")
    CASE_REQ_HEADER_APPSEC_ENABLED("cf-ray")
    CASE_REQ_HEADER_APPSEC_ENABLED("x-cloud-trace-context")
    CASE_REQ_HEADER_APPSEC_ENABLED("x-appgw-trace-id")
    CASE_REQ_HEADER_APPSEC_ENABLED("x-sigsci-requestid")
    CASE_REQ_HEADER_APPSEC_ENABLED("x-sigsci-tags")
    CASE_REQ_HEADER_APPSEC_ENABLED("akamai-user-risk")
  }  // end switch
}

void each_resp_header(const ngx_table_elt_t& h, dd::Span& span) {
#define CASE_RESP_HEADER(header)                                 \
  do {                                                           \
    if (h.key.len == (""sv header).size()) {                     \
      if (dnsec::resp_key_equals_ci(h, ""sv header)) {           \
        if (h.hash != 0) {                                       \
          span.set_tag("http.response.headers."sv header,        \
                       datadog::nginx::to_string_view(h.value)); \
        } else {                                                 \
          span.remove_tag("http.response.headers."sv header);    \
        }                                                        \
        return;                                                  \
      }                                                          \
    }                                                            \
  } while (0)

  CASE_RESP_HEADER("content-length");
  CASE_RESP_HEADER("content-type");
  CASE_RESP_HEADER("content-encoding");
  CASE_RESP_HEADER("content-language");
}

void handle_special_resp_headers(const ngx_http_headers_out_t& headers_out,
                                 dd::Span& span) {
  if (headers_out.content_type.len > 0) {
    span.set_tag("http.response.headers.content-type",
                 datadog::nginx::to_string_view(headers_out.content_type));
  }

  if (headers_out.content_length_n != -1) {
    char buffer
        [std::numeric_limits<decltype(headers_out.content_length_n)>::digits10 +
         1];
    auto [ptr, ec] = std::to_chars(buffer, buffer + sizeof(buffer),
                                   headers_out.content_length_n);
    if (ec == std::errc{}) {
      span.set_tag("http.response.headers.content-length",
                   std::string_view{buffer, static_cast<size_t>(ptr - buffer)});
    } else {
      assert(0 && "Failed to convert content length to string");
    }
  }
}
}  // namespace

namespace datadog::nginx::security {

void set_header_tags(bool has_attack, ngx_http_request_t& request,
                     dd::Span& span) {
  // Limitation: only reports the last value of each header

  // Request headers
  {
    dnsec::NgnixHeaderIterable it{request.headers_in.headers};
    for (auto&& h : it) {
      each_req_header(has_attack, h, span);
    }
  }

  // Response headers (set unconditionally when appsec is enabled)
  {
    dnsec::NgnixHeaderIterable it{request.headers_out.headers};
    for (auto&& h : it) {
      each_resp_header(h, span);
    }
    handle_special_resp_headers(request.headers_out, span);
  }
}
}  // namespace datadog::nginx::security
