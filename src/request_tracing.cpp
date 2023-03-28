#include "request_tracing.h"

#include <datadog/span.h>
#include <datadog/span_config.h>

#include <cassert>
#include <chrono>
#include <ctime>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

#include "array_util.h"
#include "dd.h"
#include "global_tracer.h"
#include "ngx_header_reader.h"
#include "ngx_http_datadog_module.h"
#include "string_util.h"
#include "tracing_library.h"

namespace datadog {
namespace nginx {

static std::string get_loc_operation_name(
    ngx_http_request_t *request, const ngx_http_core_loc_conf_t *core_loc_conf,
    const datadog_loc_conf_t *loc_conf) {
  if (loc_conf->loc_operation_name_script.is_valid())
    return to_string(loc_conf->loc_operation_name_script.run(request));
  else
    return to_string(core_loc_conf->name);
}

static std::string get_request_operation_name(
    ngx_http_request_t *request, const ngx_http_core_loc_conf_t *core_loc_conf,
    const datadog_loc_conf_t *loc_conf) {
  if (loc_conf->operation_name_script.is_valid())
    return to_string(loc_conf->operation_name_script.run(request));
  else
    return to_string(core_loc_conf->name);
}

static std::string get_loc_resource_name(ngx_http_request_t *request,
                                         const datadog_loc_conf_t *loc_conf) {
  if (loc_conf->loc_resource_name_script.is_valid()) {
    return to_string(loc_conf->loc_resource_name_script.run(request));
  } else {
    return "[invalid_resource_name_pattern]";
  }
}

static std::string get_request_resource_name(ngx_http_request_t *request,
                                             const datadog_loc_conf_t *loc_conf) {
  if (loc_conf->resource_name_script.is_valid()) {
    return to_string(loc_conf->resource_name_script.run(request));
  } else {
    return "[invalid_resource_name_pattern]";
  }
}

static void add_script_tags(ngx_array_t *tags, ngx_http_request_t *request, dd::Span &span) {
  if (!tags) return;
  auto add_tag = [&](const datadog_tag_t &tag) {
    auto key = tag.key_script.run(request);
    auto value = tag.value_script.run(request);
    if (key.data && value.data) span.set_tag(to_string(key), to_string(value));
  };
  for_each<datadog_tag_t>(*tags, add_tag);
}

static void add_status_tags(const ngx_http_request_t *request, dd::Span &span) {
  // Check for errors.
  auto status = request->headers_out.status;
  auto status_line = to_string(request->headers_out.status_line);
  if (status != 0) span.set_tag("http.status_code", std::to_string(status));
  if (status_line.data()) span.set_tag("http.status_line", status_line);
  // Treat any 5xx code as an error.
  if (status >= 500) {
    span.set_tag("error", "1");
  }
}

static void add_upstream_name(const ngx_http_request_t *request,
                              dd::Span &span) {
  if (!request->upstream || !request->upstream->upstream ||
      !request->upstream->upstream->host.data)
    return;
  auto host = request->upstream->upstream->host;
  auto host_str = to_string(host);
  span.set_tag("upstream.name", host_str);
}

// Convert the epoch denoted by epoch_seconds, epoch_milliseconds to an
// std::chrono::system_clock::time_point duration from the epoch.
static std::chrono::system_clock::time_point to_system_timestamp(
    time_t epoch_seconds, ngx_msec_t epoch_milliseconds) {
  auto epoch_duration = std::chrono::seconds{epoch_seconds} +
                        std::chrono::milliseconds{epoch_milliseconds};
  return std::chrono::system_clock::from_time_t(std::time_t{0}) +
         epoch_duration;
}

// TODO: document
static dd::TimePoint estimate_past_time_point(
    std::chrono::system_clock::time_point before) {
  dd::TimePoint now = dd::default_clock();
  auto elapsed = now.wall - before;

  dd::TimePoint result;
  result.wall = before;
  result.tick = now.tick - elapsed;
  return result;
}

RequestTracing::RequestTracing(ngx_http_request_t *request,
                               ngx_http_core_loc_conf_t *core_loc_conf,
                               datadog_loc_conf_t *loc_conf, dd::Span *parent)
    : request_{request},
      main_conf_{static_cast<datadog_main_conf_t *>(
          ngx_http_get_module_main_conf(request_, ngx_http_datadog_module))},
      core_loc_conf_{core_loc_conf},
      loc_conf_{loc_conf} {
  // `main_conf_` would be null when no `http` block appears in the nginx
  // config.  If that happens, then no handlers are installed by this module,
  // and so no `RequestTracing` objects are ever instantiated.
  assert(main_conf_);

  auto* tracer = global_tracer();
  if (!tracer) throw std::runtime_error{"no global tracer set"};

  ngx_log_debug1(NGX_LOG_DEBUG_HTTP, request_->connection->log, 0,
                 "starting Datadog request span for %p", request_);

  dd::SpanConfig config;
  auto start_timestamp =
      to_system_timestamp(request->start_sec, request->start_msec);
  config.start = estimate_past_time_point(start_timestamp);
  config.name = get_request_operation_name(request_, core_loc_conf_, loc_conf_);

  if (!parent && loc_conf_->trust_incoming_span) {
    NgxHeaderReader reader{request};
    auto maybe_span = tracer->extract_or_create_span(reader, config);
    if (auto *error = maybe_span.if_error()) {
      ngx_log_error(
          NGX_LOG_ERR, request->connection->log, 0,
          "failed to extract a Datadog span request %p: [error code %d]: %s",
          request, error->code, error->message.c_str());
    } else {
      request_span_ = std::move(*maybe_span);
    }
  }

  if (!request_span_) {
    if (parent) {
      request_span_ = parent->create_child(config);
    } else {
      request_span_ = tracer->create_span(config);
    }
  }

  if (loc_conf_->enable_locations) {
    ngx_log_debug3(
        NGX_LOG_DEBUG_HTTP, request_->connection->log, 0,
        "starting Datadog location span for \"%V\"(%p) in request %p",
        &core_loc_conf->name, loc_conf_, request_);
    dd::SpanConfig config;
    config.name = get_loc_operation_name(request_, core_loc_conf_, loc_conf_);
    span_ = request_span_->create_child(config);
  }
}

void RequestTracing::on_change_block(ngx_http_core_loc_conf_t *core_loc_conf,
                                     datadog_loc_conf_t *loc_conf) {
  on_exit_block(std::chrono::steady_clock::now());
  core_loc_conf_ = core_loc_conf;
  loc_conf_ = loc_conf;

  if (loc_conf->enable_locations) {
    ngx_log_debug3(
        NGX_LOG_DEBUG_HTTP, request_->connection->log, 0,
        "starting Datadog location span for \"%V\"(%p) in request %p",
        &core_loc_conf->name, loc_conf_, request_);
    dd::SpanConfig config;
    config.name = get_loc_operation_name(request_, core_loc_conf, loc_conf);
    assert(request_span_);  // TODO: Can we assume this?
    span_ = request_span_->create_child(config);
  }
}

dd::Span &RequestTracing::active_span() {
  if (loc_conf_->enable_locations) {
    return *span_;
  } else {
    return *request_span_;
  }
}

void RequestTracing::on_exit_block(
    std::chrono::steady_clock::time_point finish_timestamp) {
  // Set default and custom tags for the block. Many nginx variables won't be
  // available when a block is first entered, so set tags when the block is
  // exited instead.
  if (loc_conf_->enable_locations) {
    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, request_->connection->log, 0,
                   "finishing Datadog location span for %p in request %p",
                   loc_conf_, request_);
    add_script_tags(main_conf_->tags, request_, *span_);
    add_script_tags(loc_conf_->tags, request_, *span_);
    add_status_tags(request_, *span_);
    add_upstream_name(request_, *span_);

    // If the location operation name and/or resource name is dependent upon a
    // variable, it may not have been available when the span was first created,
    // so evaluate them again.
    //
    // See on_log_request below
    span_->set_name(get_loc_operation_name(request_, core_loc_conf_, loc_conf_));
    span_->set_resource_name(get_loc_resource_name(request_, loc_conf_));
    span_->set_end_time(finish_timestamp);
  } else {
    add_script_tags(loc_conf_->tags, request_, *request_span_);
  }
}

void RequestTracing::on_log_request() {
  auto finish_timestamp = std::chrono::steady_clock::now();
  on_exit_block(finish_timestamp);

  ngx_log_debug1(NGX_LOG_DEBUG_HTTP, request_->connection->log, 0,
                 "finishing Datadog request span for %p", request_);
  add_status_tags(request_, *request_span_);
  add_script_tags(main_conf_->tags, request_, *request_span_);
  add_upstream_name(request_, *request_span_);

  // When datadog_operation_name points to a variable, then it can be
  // initialized or modified at any phase of the request, so set the span
  // operation name at request exit phase, which will take the latest value of
  // the variable pointed to by the datadog_operation_name directive. Similarly
  // with resource name.
  auto core_loc_conf = static_cast<ngx_http_core_loc_conf_t *>(
      ngx_http_get_module_loc_conf(request_, ngx_http_core_module));
  request_span_->set_name(get_request_operation_name(request_, core_loc_conf, loc_conf_));
  request_span_->set_resource_name(get_request_resource_name(request_, loc_conf_));

  // Note: At this point, we could run an `NginxScript` to interrogate the
  // proxied server's response headers, e.g. to retrieve a deferred sampling
  // decision.

  request_span_->set_end_time(finish_timestamp);
}

// Expands the active span context into a list of key-value pairs and returns
// the value for `key` if it exists.
//
// Note: there's caching so that if lookup_propagation_header_variable_value is
// repeatedly called for the same active span context, it will only be expanded
// once.
//
// See propagate_datadog_context
ngx_str_t RequestTracing::lookup_propagation_header_variable_value(
    std::string_view key) {
  return propagation_header_querier_.lookup_value(request_, active_span(), key);
}

ngx_str_t RequestTracing::lookup_span_variable_value(std::string_view key) {
  return to_ngx_str(request_->pool, TracingLibrary::span_variables().resolve(
                                        key, active_span()));
}

}  // namespace nginx
}  // namespace datadog
