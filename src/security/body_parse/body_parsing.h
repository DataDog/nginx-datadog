#pragma once

extern "C" {
#include <ngx_http.h>
}

#include "../ddwaf_obj.h"

namespace datadog::nginx::security {

bool parse_body_req(ddwaf_obj &slot, const ngx_http_request_t &req,
                    const ngx_chain_t &chain, std::size_t size,
                    DdwafMemres &memres);

bool is_body_resp_parseable(const ngx_http_request_t &req);

bool parse_body_resp(ddwaf_obj &slot, const ngx_http_request_t &req,
                     const ngx_chain_t &chain, std::size_t size,
                     DdwafMemres &memres);

}  // namespace datadog::nginx::security
