#include "datadog_variable.h"
#include "ot.h"
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

const ot::string_view opentracing_context_variable_name{
    "opentracing_context_"};

static const ot::string_view opentracing_binary_context_variable_name{
    "opentracing_binary_context"};

// Extract the key specified by the variable's suffix and expand it to the
// corresponding value of the active span context.
//
// See propagate_datadog_context
static ngx_int_t expand_datadog_context_variable(
    ngx_http_request_t* request, ngx_http_variable_value_t* variable_value,
    uintptr_t data) noexcept try {
  auto variable_name = to_string_view(*reinterpret_cast<ngx_str_t*>(data));
  auto prefix_length = opentracing_context_variable_name.size();

  ot::string_view key{variable_name.data() + prefix_length,
                               variable_name.size() - prefix_length};

  auto context = get_datadog_context(request);
  // Context can be null if tracing is disabled.
  if (context == nullptr) {
    ngx_log_debug2(NGX_LOG_DEBUG_EVENT, request->connection->log, 0,
		   "failed to expand %V: no DatadogContext attached to request %p",
		   data, request);
    return NGX_ERROR;
  }

  auto span_context_value = context->lookup_span_context_value(request, key);

  variable_value->len = span_context_value.len;
  variable_value->valid = 1;
  variable_value->no_cacheable = 1;
  variable_value->not_found = 0;
  variable_value->data = span_context_value.data;

  return NGX_OK;
} catch (const std::exception& e) {
  ngx_log_error(NGX_LOG_ERR, request->connection->log, 0,
                "failed to expand %V"
                " for request %p: %s",
                data, request, e.what());
  return NGX_ERROR;
}

static ngx_int_t expand_datadog_binary_context_variable(
    ngx_http_request_t* request, ngx_http_variable_value_t* variable_value,
    uintptr_t data) noexcept try {
  auto context = get_datadog_context(request);
  if (context == nullptr) {
    throw std::runtime_error{"no DatadogContext attached to request"};
  }
  auto binary_context = context->get_binary_context(request);
  variable_value->len = binary_context.len;
  variable_value->valid = 1;
  variable_value->no_cacheable = 1;
  variable_value->not_found = 0;
  variable_value->data = binary_context.data;

  return NGX_OK;
} catch (const std::exception& e) {
  ngx_log_error(NGX_LOG_ERR, request->connection->log, 0,
                "failed to expand %s"
                " for request %p: %s",
                opentracing_context_variable_name.data(), request, e.what());
  return NGX_ERROR;
}

// TODO: hack hack
static void print_module_names(const ngx_cycle_t *cycle) noexcept {
  std::cout << "BEGIN print module names in " __FILE__ "\n";
  for (int i = 0; i < cycle->modules_n; ++i) {
    std::cout << "cycle has module: " << cycle->modules[i]->name << "\n";
  }
  std::cout << "END print module names\n";
}
// end TODO

ngx_int_t add_variables(ngx_conf_t* cf) noexcept {
  // TODO: hack hack
  print_module_names((const ngx_cycle_t*)(ngx_cycle));
  print_module_names(cf->cycle);
  // end TODO
  auto opentracing_context = to_ngx_str(opentracing_context_variable_name);
  auto opentracing_context_var = ngx_http_add_variable(
      cf, &opentracing_context,
      NGX_HTTP_VAR_NOCACHEABLE | NGX_HTTP_VAR_NOHASH | NGX_HTTP_VAR_PREFIX);
  opentracing_context_var->get_handler = expand_datadog_context_variable;
  opentracing_context_var->data = 0;

  auto opentracing_binary_context =
      to_ngx_str(opentracing_binary_context_variable_name);
  auto opentracing_binary_context_var = ngx_http_add_variable(
      cf, &opentracing_binary_context, NGX_HTTP_VAR_NOCACHEABLE);
  opentracing_binary_context_var->get_handler =
      expand_datadog_binary_context_variable;
  opentracing_binary_context_var->data = 0;

  return NGX_OK;
}
}  // namespace nginx
}  // namespace datadog
