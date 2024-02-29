#pragma once

#include <ddwaf.h>
#include "ddwaf_memres.h"

extern "C" {
#include <ngx_http.h>
}

namespace datadog {
namespace nginx {
namespace security {

ddwaf_object *collect_request_data(const ngx_http_request_t &request,
                                   ddwaf_memres &memres);
}  // namespace security
}  // namespace nginx
}  // namespace datadog
