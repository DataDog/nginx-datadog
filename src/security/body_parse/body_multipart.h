#pragma once

#include "../ddwaf_obj.h"
#include "header.h"

extern "C" {
#include <ngx_core.h>
#include <ngx_http.h>
}

namespace datadog::nginx::security {

bool parse_multipart(ddwaf_obj& slot, const ngx_http_request_t& req,
                     HttpContentType& ct, const ngx_chain_t& chain,
                     DdwafMemres& memres);

}  // namespace datadog::nginx::security
