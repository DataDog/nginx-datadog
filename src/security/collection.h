#pragma once

#include <ddwaf.h>

#include <optional>

#include "ddwaf_memres.h"

extern "C" {
#include <ngx_http.h>
}

namespace datadog::nginx::security {

ddwaf_object *collect_request_data(const ngx_http_request_t &request,
                                   const std::optional<std::string> &client_ip,
                                   DdwafMemres &memres);
ddwaf_object *collect_response_data(const ngx_http_request_t &request,
                                    DdwafMemres &memres);
}  // namespace datadog::nginx::security
