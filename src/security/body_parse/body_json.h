#pragma once

#include "../ddwaf_obj.h"

extern "C" {
#include <ngx_core.h>
#include <ngx_http.h>
}

namespace datadog::nginx::security {

bool parse_json(ddwaf_obj& slot, const ngx_http_request_t& req,
                const ngx_chain_t& chain, size_t limit, DdwafMemres& memres);

}  // namespace datadog::nginx::security
