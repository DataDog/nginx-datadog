#include "datadog_variable.h"
#include "ot.h"
#include "tracing_library.h"
#include "ngx_http_datadog_module.h"

#include "datadog_context.h"
#include "utility.h"

#include <opentracing/string_view.h>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <iostream> // TODO: no
#include <limits>
#include <stdexcept>

namespace datadog {
namespace nginx {

// TODO: document
static ngx_int_t expand_span_variable(
    ngx_http_request_t* request, ngx_http_variable_value_t* variable_value,
    uintptr_t data) noexcept try {
  auto variable_name = to_string_view(*reinterpret_cast<ngx_str_t*>(data));
  auto prefix_length = TracingLibrary::span_variables().prefix.size();
  auto suffix = slice(variable_name, prefix_length);

  auto context = get_datadog_context(request);
  // Context can be null if tracing is disabled.
  if (context == nullptr) {
    // TODO: Is this how we make it print "-"?
    variable_value->valid = 1;  // TODO: don't know
    variable_value->no_cacheable = true;
    variable_value->not_found = true;
    return NGX_OK;
  }

  std::cout << "*)*(*)*(*)*(*)*(*(*)*(*(*)*)* looking up span variable suffix: " << suffix << "\n";
  auto span_variable_value = context->lookup_span_variable_value(request, suffix);
  std::cout << "*)*(*)*(*)*(*)*(*(*)*(*(*)*)* that resulted in: " << str(span_variable_value) << "\n";

  variable_value->len = span_variable_value.len;
  variable_value->valid = true;
  variable_value->no_cacheable = true;
  variable_value->not_found = false;
  variable_value->data = span_variable_value.data;

  return NGX_OK;
} catch (const std::exception& e) {
  ngx_log_error(NGX_LOG_ERR, request->connection->log, 0,
                "failed to expand %V"
                " for request %p: %s",
                data, request, e.what());
  return NGX_ERROR;
}

// TODO: document
static ngx_int_t expand_propagation_header_variable(
    ngx_http_request_t* request, ngx_http_variable_value_t* variable_value,
    uintptr_t data) noexcept try {
  auto variable_name = to_string_view(*reinterpret_cast<ngx_str_t*>(data));
  auto prefix_length = TracingLibrary::propagation_header_variable_name_prefix().size();
  auto suffix = slice(variable_name, prefix_length);

  auto context = get_datadog_context(request);
  // Context can be null if tracing is disabled.
  if (context == nullptr) {
    // TODO: Is this how we make it print "-"?
    variable_value->valid = 1;  // TODO: don't know
    variable_value->no_cacheable = true;
    variable_value->not_found = true;
    return NGX_OK;
  }

  std::cout << "*)*(*)*(*)*(*)*(*(*)*(*(*)*)* looking up header variable suffix: " << suffix << "\n";
  auto value = context->lookup_propagation_header_variable_value(request, suffix);
  std::cout << "*)*(*)*(*)*(*)*(*(*)*(*(*)*)* that resulted in: " << str(value) << "\n";

  variable_value->len = value.len;
  variable_value->valid = true;
  variable_value->no_cacheable = true;
  variable_value->not_found = false;
  variable_value->data = value.data;

  return NGX_OK;
} catch (const std::exception& e) {
  ngx_log_error(NGX_LOG_ERR, request->connection->log, 0,
                "failed to expand %V"
                " for request %p: %s",
                data, request, e.what());
  return NGX_ERROR;
}

ngx_int_t add_variables(ngx_conf_t* cf) noexcept {
  ngx_str_t prefix;
  ngx_http_variable_t *variable;

  // Register the variable name prefix for span variables.
  prefix = to_ngx_str(TracingLibrary::span_variables().prefix);
  variable = ngx_http_add_variable(
      cf, &prefix,
      NGX_HTTP_VAR_NOCACHEABLE | NGX_HTTP_VAR_NOHASH | NGX_HTTP_VAR_PREFIX);
  variable->get_handler = expand_span_variable;
  variable->data = 0;

  // Register the variable name prefix for propagation header variables.
  prefix = to_ngx_str(TracingLibrary::propagation_header_variable_name_prefix());
  variable = ngx_http_add_variable(
      cf, &prefix,
      NGX_HTTP_VAR_NOCACHEABLE | NGX_HTTP_VAR_NOHASH | NGX_HTTP_VAR_PREFIX);
  variable->get_handler = expand_propagation_header_variable;
  variable->data = 0;

  return NGX_OK;
}
}  // namespace nginx
}  // namespace datadog
