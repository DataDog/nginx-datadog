#pragma once

extern "C" {
#include <nginx.h>
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
}

namespace datadog {
namespace nginx {

// `DatadogConfHandlerArgs` contains the parameters to the
// `datadog_conf_handler` function.
struct DatadogConfHandlerConfig {
    // `conf` is the nginx configuration that's currently being interpreted.
    ngx_conf_t* conf;
    // `skip_this_module` is whether to skip configuration handlers defined in
    // this module.  This is used to "hijack" configuration directives defined
    // in other modules: we define a handler with the same name, do some work,
    // and then dispatch to the other module's implementation.  In order to
    // access the other module's implementation, we have to skip over our own
    // module.
    bool skip_this_module;
};

// `datadog_conf_handler` originated as a copy of
//    https://github.com/nginx/nginx/blob/0ad556fe59ad132dc4d34dea9e80f2ff2c3c1314/src/core/ngx_conf_file.c
// this is necessary for our implementation of context propagation.
//
// See http://mailman.nginx.org/pipermail/nginx-devel/2018-March/011008.html
ngx_int_t datadog_conf_handler(const DatadogConfHandlerConfig& args) noexcept;
}  // namespace nginx
}  // namespace datadog
