#pragma once

extern "C" {

#include <nginx.h>
#include <ngx_conf_file.h>
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <ngx_log.h>
}

#include <optional>
#include <string>

namespace datadog::common {

/// Extracted from NGINX Development Guide
/// (<https://nginx.org/en/docs/dev/development_guide.html#http_complex_values>)
///
/// A complex value, despite its name, provides an easy way to evaluate
/// expressions which can contain text, variables, and their combination.
ngx_http_complex_value_t* make_complex_value(ngx_conf_t* cf,
                                             ngx_str_t& default_value);

ngx_http_complex_value_t* make_complex_value(ngx_conf_t* cf,
                                             std::string_view default_value);

/// Evaluate complex expressions. Returns the value if the evaluation is
/// successful, otherwise returns nothing.
std::optional<std::string> eval_complex_value(
    ngx_http_complex_value_t* complex_value, ngx_http_request_t* request);

}  // namespace datadog::common
