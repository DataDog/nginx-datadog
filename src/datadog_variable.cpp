#include "datadog_variable.h"

#include <datadog/telemetry/telemetry.h>
#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <algorithm>
#include <cstdlib>

#include "datadog_context.h"
#include "global_tracer.h"
#include "ngx_http_datadog_module.h"
#include "string_util.h"
#include "telemetry_util.h"
#include "tracing_library.h"

namespace datadog {
namespace nginx {
namespace {

// Return whether the specified `request` is a subrequest for which tracing
// ("logging") is disabled.
bool is_untraced_subrequest(ngx_http_request_t *request) {
  auto core_loc_conf = static_cast<ngx_http_core_loc_conf_t *>(
      ngx_http_get_module_loc_conf(request, ngx_http_core_module));

  return request->parent != nullptr && !core_loc_conf->log_subrequest;
}

}  // namespace

// Load into the specified `variable_value` the result of looking up the value
// of the variable name indicated by the specified `data`.  The variable name,
// if valid, will resolve to some property on the active span, i.e.
// `datadog_trace_id` resolves to a string containing the trace ID.  Return
// `NGX_OK` on success or another value if an error occurs.
static ngx_int_t expand_span_variable(ngx_http_request_t *request,
                                      ngx_http_variable_value_t *variable_value,
                                      uintptr_t data) noexcept try {
  auto context = get_datadog_context(request);
  // Context can be null if tracing is disabled.
  if (context == nullptr || is_untraced_subrequest(request)) {
    const ngx_str_t not_found_str = ngx_string("-");
    variable_value->len = not_found_str.len;
    variable_value->data = not_found_str.data;
    variable_value->valid = 1;
    variable_value->no_cacheable = true;
    variable_value->not_found = false;
    return NGX_OK;
  }

  auto set_variable = [&](std::string_view suffix) {
    auto span_variable_value =
        context->lookup_span_variable_value(request, suffix);
    variable_value->len = span_variable_value.len;
    variable_value->valid = true;
    variable_value->no_cacheable = true;
    variable_value->not_found = false;
    variable_value->data = span_variable_value.data;
  };

  auto variable_name = to_string_view(*reinterpret_cast<ngx_str_t *>(data));
  if (variable_name.starts_with("datadog_")) {
    auto suffix =
        slice(variable_name, TracingLibrary::span_variables().prefix.size());
    set_variable(suffix);
  } else if (std::string_view otel("opentelemetry_");
             variable_name.starts_with(otel)) {
    auto suffix = slice(variable_name, otel.size());
    set_variable(suffix);
  }

  return NGX_OK;
} catch (const std::exception &e) {
  telemetry::report_error_log(e.what(), CURRENT_FRAME(request));
  ngx_log_error(NGX_LOG_ERR, request->connection->log, 0,
                "failed to expand %V"
                " for request %p: %s",
                data, request, e.what());
  return NGX_ERROR;
}

// Load into the specified `variable_value` the result of looking up the value
// of the variable name indicated by the specified `data`.  The variable name,
// if valid, will resolve to some environment variable for the current process,
// e.g.  `datadog_env_dd_agent_host` resolves to a string containing the value
// of the "DD_AGENT_HOST" environment variable as the current process inherited
// it.  Only a subset of environment variables may be looked up this way --
// only the environment variables listed in
// `TracingLibrary::environment_variable_names`.  Return `NGX_OK` on success or
// another value if an error occurs.
static ngx_int_t expand_environment_variable(
    ngx_http_request_t *request, ngx_http_variable_value_t *variable_value,
    uintptr_t data) noexcept {
  auto variable_name = to_string_view(*reinterpret_cast<ngx_str_t *>(data));
  auto prefix_length =
      TracingLibrary::environment_variable_name_prefix().size();
  auto suffix = slice(variable_name, prefix_length);

  std::string env_var_name{suffix.data(), suffix.size()};
  std::transform(env_var_name.begin(), env_var_name.end(), env_var_name.begin(),
                 to_upper);

  const auto allow_list = TracingLibrary::environment_variable_names();
  const char *env_value = nullptr;
  if (std::find(allow_list.begin(), allow_list.end(), env_var_name) !=
      allow_list.end()) {
    env_value = std::getenv(env_var_name.c_str());
  }

  if (env_value == nullptr) {
    const ngx_str_t not_found_str = ngx_string("-");
    variable_value->len = not_found_str.len;
    variable_value->data = not_found_str.data;
    variable_value->valid = true;
    variable_value->no_cacheable = true;
    variable_value->not_found = false;
    return NGX_OK;
  }

  const ngx_str_t value_str = to_ngx_str(request->pool, env_value);
  variable_value->len = value_str.len;
  variable_value->valid = true;
  variable_value->no_cacheable = true;
  variable_value->not_found = false;
  variable_value->data = value_str.data;

  return NGX_OK;
}

// Load into the specified `variable_value` the result of looking up the value
// of the variable whose name is determined by
// `TracingLibrary::configuration_json_variable_name()`.  The variable
// evaluates to a JSON representation of the tracer configuration.  Return
// `NGX_OK` on success or another value if an error occurs.
static ngx_int_t expand_configuration_variable(
    ngx_http_request_t *request, ngx_http_variable_value_t *variable_value,
    uintptr_t /*data*/) noexcept {
  variable_value->valid = true;
  variable_value->no_cacheable = true;
  variable_value->not_found = false;

  const dd::Tracer *tracer = global_tracer();
  if (tracer == nullptr) {
    // No tracer, no config. Evaluate to "-" (hyphen).
    const ngx_str_t not_found_str = ngx_string("-");
    variable_value->len = not_found_str.len;
    variable_value->data = not_found_str.data;
    return NGX_OK;
  }

  // NOTE(@dmehala): Override the configuration with runtime configuration.
  // We should find a better way to generate the configuration.
  rapidjson::Document doc;
  if (doc.Parse(tracer->config().c_str()).HasParseError()) {
    auto errorCode = doc.GetParseError();
    ngx_log_error(NGX_LOG_ERR, request->connection->log, 0,
                  "failed to parse tracer configuration: %d", errorCode);
    const ngx_str_t json_str = to_ngx_str(request->pool, tracer->config());
    variable_value->len = json_str.len;
    variable_value->data = json_str.data;
  }

  auto &alloc = doc.GetAllocator();

  // override
  const auto &loc_conf = *static_cast<datadog::nginx::datadog_loc_conf_t *>(
      ngx_http_get_module_loc_conf(request, ngx_http_datadog_module));

  auto append_to_doc = [&](std::string_view key,
                           ngx_http_complex_value_t *value) {
    if (value == nullptr) return;

    ngx_str_t res;
    if (ngx_http_complex_value(request, value, &res) == NGX_OK &&
        res.len != 0) {
      doc.AddMember(rapidjson::Value(key.data(), key.size(), alloc),
                    rapidjson::Value((char *)res.data, res.len, alloc), alloc);
    }
  };

  append_to_doc("service", loc_conf.service_name);
  append_to_doc("environment", loc_conf.service_env);
  append_to_doc("version", loc_conf.service_version);

  rapidjson::StringBuffer buffer;
  rapidjson::Writer<rapidjson::StringBuffer> writer(buffer);
  doc.Accept(writer);

  const ngx_str_t json_str = to_ngx_str(request->pool, buffer.GetString());
  variable_value->len = json_str.len;
  variable_value->data = json_str.data;
  return NGX_OK;
}

// Load into the specified `variable_value` the result of looking up the value
// of the variable whose name is determined by
// `TracingLibrary::location_variable_name()`.  The variable evaluates to the
// pattern or name associated with the location block chosen for processing
// `request`.
//
// For example,
//
//     location /foo {
//         # ...
//     }
//
// has location name "/foo", while
//
//     location ~ /api/v(1|2)/trace/[0-9]+ {
//         # ...
//     }
//
// has location name "/api/v(1|2)/trace/[0-9]+".
//
// Named locations have their literal names, including the "@", e.g.
//
//     location @updates {
//         # ...
//     }
//
// has location name "@updates".
//
// If there is no location associated with `request`, then load into
// `variable_value` a hyphen character ("-").
//
// Return `NGX_OK` on success or another value if an error occurs.
static ngx_int_t expand_location_variable(
    ngx_http_request_t *request, ngx_http_variable_value_t *variable_value,
    uintptr_t /*data*/) noexcept {
  const auto core_loc_conf = static_cast<ngx_http_core_loc_conf_t *>(
      ngx_http_get_module_loc_conf(request, ngx_http_core_module));

  if (core_loc_conf == nullptr) {
    const ngx_str_t not_found_str = ngx_string("-");
    variable_value->len = not_found_str.len;
    variable_value->data = not_found_str.data;
    variable_value->valid = true;
    variable_value->no_cacheable = true;
    variable_value->not_found = false;
    return NGX_OK;
  }

  const ngx_str_t value_str = core_loc_conf->name;
  variable_value->len = value_str.len;
  variable_value->valid = true;
  variable_value->no_cacheable = true;
  variable_value->not_found = false;
  variable_value->data = value_str.data;

  return NGX_OK;
}

ngx_int_t add_variables(ngx_conf_t *cf) noexcept {
  ngx_str_t prefix;
  ngx_http_variable_t *variable;

  // Register the variable name prefix for span variables.
  prefix = to_ngx_str(TracingLibrary::span_variables().prefix);
  variable = ngx_http_add_variable(
      cf, &prefix,
      NGX_HTTP_VAR_NOCACHEABLE | NGX_HTTP_VAR_NOHASH | NGX_HTTP_VAR_PREFIX);
  variable->get_handler = expand_span_variable;
  variable->data = 0;

  // Register the variable name prefix for Datadog-relevant environment
  // variables.
  prefix = to_ngx_str(TracingLibrary::environment_variable_name_prefix());
  variable = ngx_http_add_variable(
      cf, &prefix,
      NGX_HTTP_VAR_NOCACHEABLE | NGX_HTTP_VAR_NOHASH | NGX_HTTP_VAR_PREFIX);
  variable->get_handler = expand_environment_variable;
  variable->data = 0;

  // Register the variable name prefix for OpenTelemetry-relevant environment
  // variables.
  prefix = ngx_string("opentelemetry_");
  variable = ngx_http_add_variable(
      cf, &prefix,
      NGX_HTTP_VAR_NOCACHEABLE | NGX_HTTP_VAR_NOHASH | NGX_HTTP_VAR_PREFIX);
  variable->get_handler = expand_span_variable;
  variable->data = 0;

  // Register the variable name for getting the tracer configuration.
  ngx_str_t name =
      to_ngx_str(TracingLibrary::configuration_json_variable_name());
  variable = ngx_http_add_variable(cf, &name, NGX_HTTP_VAR_NOHASH);
  variable->get_handler = expand_configuration_variable;
  variable->data = 0;

  // Register the variable name for getting a request's location name.
  name = to_ngx_str(TracingLibrary::location_variable_name());
  variable = ngx_http_add_variable(cf, &name, NGX_HTTP_VAR_NOHASH);
  variable->get_handler = expand_location_variable;
  variable->data = 0;

  return NGX_OK;
}
}  // namespace nginx
}  // namespace datadog
