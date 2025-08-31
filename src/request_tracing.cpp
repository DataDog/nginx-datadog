#include "request_tracing.h"

#include <datadog/dict_writer.h>
#include <datadog/span.h>
#include <datadog/span_config.h>
#include <datadog/trace_segment.h>

#include <cassert>
#include <chrono>
#include <ctime>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>

#include "array_util.h"
#include "common/variable.h"
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
  auto v =
      common::eval_complex_value(loc_conf->loc_operation_name_script, request);
  return v.value_or(to_string(core_loc_conf->name));
}

static std::string get_request_operation_name(
    ngx_http_request_t *request, const ngx_http_core_loc_conf_t *core_loc_conf,
    const datadog_loc_conf_t *loc_conf) {
  auto v = common::eval_complex_value(loc_conf->operation_name_script, request);
  return v.value_or(to_string(core_loc_conf->name));
}

static std::string get_loc_resource_name(ngx_http_request_t *request,
                                         const datadog_loc_conf_t *loc_conf) {
  auto v =
      common::eval_complex_value(loc_conf->loc_resource_name_script, request);
  return v.value_or("[invalid_resource_name_pattern]");
}

static std::string get_request_resource_name(
    ngx_http_request_t *request, const datadog_loc_conf_t *loc_conf) {
  auto v = common::eval_complex_value(loc_conf->resource_name_script, request);
  return v.value_or("[invalid_resource_name_pattern]");
}

static void add_script_tags(
    const std::unordered_map<std::string, ngx_http_complex_value_t *> &tags,
    ngx_http_request_t *request, dd::Span &span) {
  for (const auto &[key, complex_value] : tags) {
    auto value = common::eval_complex_value(complex_value, request);
    if (value) span.set_tag(key, std::move(*value));
  }
}

static void add_status_tags(const ngx_http_request_t *request, dd::Span &span) {
  // Check for errors.
  auto status = request->headers_out.status;
  auto status_line = to_string(request->headers_out.status_line);
  if (status != 0) span.set_tag("http.status_code", std::to_string(status));
  if (status_line.data()) span.set_tag("http.status_line", status_line);
  // Treat any 5xx code as an error.
  if (status >= 500) {
    span.set_error(true);
  }
}

// Search through `conf` and its ancestors for the first `baggage_span_tags`
// directive and copy the configured tags. If no tags are configured, then add the default tags,
// which are stored in the main conf.
// 
// Precondition: The local conf has the directive `datadog_baggage_span_tags_enabled` set.
void add_baggage_span_tags(datadog_loc_conf_t *conf, datadog_main_conf_t *main_conf, tracing::Baggage& baggage, dd::Span &span) {
  if (baggage.empty()) return;

  static const std::string baggage_prefix = "baggage.";

  do {
    if (!conf->baggage_span_tags.empty()) {
      if (baggage.size() == 1 && conf->baggage_span_tags.contains("*")) {
        baggage.visit([&span](std::string_view key, std::string_view value) {
            span.set_tag(baggage_prefix + std::string(key), value);
        });
      }
      else {
        for (const auto &tag_name : conf->baggage_span_tags) {
          if (baggage.contains(tag_name)) {
            span.set_tag(baggage_prefix + tag_name, baggage.get(tag_name).value());
          }
        }
      }

      return;
    }
 
    conf = conf->parent;
  } while (conf);

  // At this point, no baggage span tags were found in the conf or its ancestors.
  // Apply the main conf default baggage span tags.
  for (const auto &tag_name : main_conf->baggage_span_tags) {
    span.set_tag(baggage_prefix + tag_name, baggage.get(tag_name).value());
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

// dd-trace-cpp uses steady time to calculate span duration, but nginx provides
// only system time.
// `estimate_past_time_point` guesses the steady time corresponding to the
// specified system time (`before`) by comparing `before` with the current
// system time.
// Return a `dd::TimePoint` containing the specified system (wall) time
// (`before`) and the calculated steady (tick) time.
static dd::TimePoint estimate_past_time_point(
    std::chrono::system_clock::time_point before) {
  dd::TimePoint now = dd::default_clock();
  const auto elapsed = now.wall - before;

  dd::TimePoint result;
  result.wall = before;
  result.tick = now.tick;
  if (elapsed > decltype(elapsed)::zero()) {
    result.tick -= elapsed;
  }
  return result;
}

// Search through `conf` and its ancestors for the first `datadog_sample_rate`
// directive whose condition is satisfied for the specified `request`. If there
// is such a `datadog_sample_rate`, then on the specified `span` set the
// "nginx.sample_rate_source" tag to a value that identifies the particular
// `datadog_sample_rate` directive. A sampling rule previously configured in the
// tracer will then match on the tag value and apply the sample rate from the
// `datadog_sample_rate` directive.
void set_sample_rate_tag(ngx_http_request_t *request, datadog_loc_conf_t *conf,
                         dd::Span &span) {
  do {
    for (const datadog_sample_rate_condition_t &rate : conf->sample_rates) {
      const ngx_str_t expression = rate.condition.run(request);
      if (str(expression) == "on") {
        span.set_tag(rate.tag_name(), rate.tag_value());
        return;
      }
      if (str(expression) != "off") {
        ngx_log_error(NGX_LOG_ERR, request->connection->log, 0,
                      "Condition expression for %V directive at %s evaluated "
                      "to unexpected value "
                      "\"%V\". Expected \"on\" or \"off\". Proceeding as if it "
                      "were \"off\".",
                      &rate.directive.directive_name, rate.tag_value().c_str(),
                      &expression);
      }
    }

    conf = conf->parent;
  } while (conf);
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

  auto *tracer = global_tracer();
  if (!tracer) throw std::runtime_error{"no global tracer set"};

  ngx_log_debug1(NGX_LOG_DEBUG_HTTP, request_->connection->log, 0,
                 "starting Datadog request span for %p", request_);

  std::optional<std::string> service =
      common::eval_complex_value(loc_conf_->service_name, request_);
  std::optional<std::string> env =
      common::eval_complex_value(loc_conf_->service_env, request_);
  std::optional<std::string> version =
      common::eval_complex_value(loc_conf_->service_version, request_);

  dd::SpanConfig config;

  auto start_timestamp =
      to_system_timestamp(request->start_sec, request->start_msec);
  config.service = service;
  config.environment = env;
  config.version = version;
  config.start = estimate_past_time_point(start_timestamp);
  config.name = get_request_operation_name(request_, core_loc_conf_, loc_conf_);
  config.resource = get_request_resource_name(request_, loc_conf_);

  // By the end of this function, we will have a `request_span_`.
  //
  // It could be that we were given a `parent`, in which case `request_span_`
  // will be a child of that parent.
  //
  // Alternatively, it could be that we don't have a parent, and also we don't
  // trust the request headers enough to extract trace context from them.
  // In that case, we will create a new trace for `request_span_`.
  //
  // If we don't have a parent and we _do_ trust request headers, then we will
  // try to extract trace context from the headers. It could be that there is no
  // context to extract, or it could be that the extracted data is invalid. In
  // both cases, we fall back to creating a new trace for `request_span_`. If,
  // on the other hand, extracting trace context from the request headers
  // succeeds, then `request_span_` is part of the extracted trace.
  if (!parent && loc_conf_->trust_incoming_span) {
    NgxHeaderReader reader{&request->headers_in.headers};
    auto maybe_span = tracer->extract_span(reader, config);
    if (auto *error = maybe_span.if_error()) {
      if (error->code != dd::Error::NO_SPAN_TO_EXTRACT) {
        ngx_log_error(
            NGX_LOG_ERR, request->connection->log, 0,
            "failed to extract a Datadog span request %p: [error code %d]: %s",
            request, error->code, error->message.c_str());
      }
      request_span_.emplace(tracer->create_span(config));
    } else {
      request_span_.emplace(std::move(*maybe_span));
    }

    if (loc_conf_->baggage_span_tags_enabled) {
      auto maybe_baggage = tracer->extract_baggage(reader);
      if (maybe_baggage && request_span_) {
        add_baggage_span_tags(loc_conf, main_conf_, *maybe_baggage, *request_span_);
      }
    }
  }

  if (!request_span_) {
    if (parent) {
      request_span_.emplace(parent->create_child(config));
    } else {
      request_span_.emplace(tracer->create_span(config));
    }
  }

  if (loc_conf_->enable_locations) {
    ngx_log_debug3(
        NGX_LOG_DEBUG_HTTP, request_->connection->log, 0,
        "starting Datadog location span for \"%V\"(%p) in request %p",
        &core_loc_conf->name, loc_conf_, request_);
    dd::SpanConfig config;
    config.service = service;
    config.environment = env;
    config.version = version;
    config.name = get_loc_operation_name(request_, core_loc_conf_, loc_conf_);
    span_.emplace(request_span_->create_child(config));
  }

  // We care about sampling rules for the request span only, because it's the
  // only span that could be the root span.
  set_sample_rate_tag(request_, loc_conf_, *request_span_);
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
    config.service =
        common::eval_complex_value(loc_conf_->service_name, request_);
    config.environment =
        common::eval_complex_value(loc_conf_->service_env, request_);
    config.version =
        common::eval_complex_value(loc_conf_->service_version, request_);
    config.name = get_loc_operation_name(request_, core_loc_conf, loc_conf);

    assert(request_span_);  // postcondition of our constructor
    span_.emplace(request_span_->create_child(config));
  }

  // We care about sampling rules for the request span only, because it's the
  // only span that could be the root span.
  set_sample_rate_tag(request_, loc_conf_, *request_span_);
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
    span_->set_name(
        get_loc_operation_name(request_, core_loc_conf_, loc_conf_));
    span_->set_resource_name(get_loc_resource_name(request_, loc_conf_));
    span_->set_end_time(std::move(finish_timestamp));
  } else {
    add_script_tags(main_conf_->tags, request_, *request_span_);
    add_script_tags(loc_conf_->tags, request_, *request_span_);
  }

  // We care about sampling rules for the request span only, because it's the
  // only span that could be the root span.
  set_sample_rate_tag(request_, loc_conf_, *request_span_);
}

void RequestTracing::on_log_request() {
  auto finish_timestamp = std::chrono::steady_clock::now();
  on_exit_block(finish_timestamp);

  ngx_log_debug1(NGX_LOG_DEBUG_HTTP, request_->connection->log, 0,
                 "finishing Datadog request span for %p", request_);
  add_status_tags(request_, *request_span_);
  add_upstream_name(request_, *request_span_);

  request_span_->set_end_time(finish_timestamp);
}

ngx_str_t RequestTracing::lookup_span_variable_value(std::string_view key) {
  return to_ngx_str(request_->pool, TracingLibrary::span_variables().resolve(
                                        key, active_span()));
}

}  // namespace nginx
}  // namespace datadog
