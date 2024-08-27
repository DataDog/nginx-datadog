#pragma once

extern "C" {
#include <ngx_http.h>
}

#include "../ddwaf_obj.h"

namespace datadog::nginx::security {

bool parse_body(ddwaf_obj &slot, ngx_http_request_t &req,
                const ngx_chain_t &chain, std::size_t size,
                DdwafMemres &memres);

}  // namespace datadog::nginx::security
