#include "request_tracing.h"
#include "extract_span_context.h"
#include "ot.h"
#include "ngx_http_datadog_module.h"

#include "utility.h"

#include <sstream>
#include <stdexcept>
#include <iostream> // TODO: hack

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

static void add_script_tags(ngx_array_t *tags, ngx_http_request_t *request,
                            ot::Span &span) {
  if (!tags) return;
  auto add_tag = [&](const datadog_tag_t &tag) {
    auto key = tag.key_script.run(request);
    auto value = tag.value_script.run(request);
    if (key.data && value.data) span.SetTag(to_string(key), to_string(value));
  };
  for_each<datadog_tag_t>(*tags, add_tag);
}

static void add_status_tags(const ngx_http_request_t *request,
                            ot::Span &span) {
  // Check for errors.
  auto status = request->headers_out.status;
  auto status_line = to_string(request->headers_out.status_line);
  if (status != 0) span.SetTag("http.status_code", status);
  if (status_line.data()) span.SetTag("http.status_line", status_line);
  // Treat any 5xx code as an error.
  if (status >= 500) {
    span.SetTag("error", true);
    span.Log({{"event", "error"}, {"message", status_line}});
  }
}

static void add_upstream_name(const ngx_http_request_t *request,
                              ot::Span &span) {
  if (!request->upstream || !request->upstream->upstream ||
      !request->upstream->upstream->host.data)
    return;
  auto host = request->upstream->upstream->host;
  auto host_str = to_string(host);
  span.SetTag("upstream.name", host_str);
}

RequestTracing::RequestTracing(
    ngx_http_request_t *request, ngx_http_core_loc_conf_t *core_loc_conf,
    datadog_loc_conf_t *loc_conf,
    const ot::SpanContext *parent_span_context)
    : request_{request},
      main_conf_{
          static_cast<datadog_main_conf_t *>(ngx_http_get_module_main_conf(
              request_, ngx_http_datadog_module))},
      core_loc_conf_{core_loc_conf},
      loc_conf_{loc_conf} {
  auto tracer = ot::Tracer::Global();
  if (!tracer) throw std::runtime_error{"no global tracer set"};

  std::unique_ptr<ot::SpanContext> extracted_context = nullptr;
  if (parent_span_context == nullptr && loc_conf_->trust_incoming_span) {
    extracted_context = extract_span_context(*tracer, request_);
    parent_span_context = extracted_context.get();
  }

  ngx_log_debug1(NGX_LOG_DEBUG_HTTP, request_->connection->log, 0,
                 "starting Datadog request span for %p", request_);
  request_span_ = tracer->StartSpan(
      get_request_operation_name(request_, core_loc_conf_, loc_conf_),
      {ot::ChildOf(parent_span_context),
       ot::StartTimestamp{
           to_system_timestamp(request->start_sec, request->start_msec)}});
  if (!request_span_) throw std::runtime_error{"tracer->StartSpan failed"};

  if (loc_conf_->enable_locations) {
    ngx_log_debug3(
        NGX_LOG_DEBUG_HTTP, request_->connection->log, 0,
        "starting Datadog location span for \"%V\"(%p) in request %p",
        &core_loc_conf->name, loc_conf_, request_);
    span_ = tracer->StartSpan(
        get_loc_operation_name(request_, core_loc_conf_, loc_conf_),
        {ot::ChildOf(&request_span_->context())});
    if (!span_) throw std::runtime_error{"tracer->StartSpan failed"};
  }
}

void RequestTracing::on_change_block(ngx_http_core_loc_conf_t *core_loc_conf,
                                     datadog_loc_conf_t *loc_conf) {
  on_exit_block();
  core_loc_conf_ = core_loc_conf;
  loc_conf_ = loc_conf;

  if (loc_conf->enable_locations) {
    ngx_log_debug3(
        NGX_LOG_DEBUG_HTTP, request_->connection->log, 0,
        "starting Datadog location span for \"%V\"(%p) in request %p",
        &core_loc_conf->name, loc_conf_, request_);
    span_ = request_span_->tracer().StartSpan(
        get_loc_operation_name(request_, core_loc_conf, loc_conf),
        {ot::ChildOf(&request_span_->context())});
    if (!span_) throw std::runtime_error{"tracer->StartSpan failed"};
  }
}

const ot::Span &RequestTracing::active_span() const {
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

    // If the location operation name is dependent upon a variable, it may not
    // have been available when the span was first created, so set the operation
    // name again.
    //
    // See on_log_request below
    span_->SetOperationName(
        get_loc_operation_name(request_, core_loc_conf_, loc_conf_));

    span_->Finish({ot::FinishTimestamp{finish_timestamp}});
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

  // When opentracing_operation_name points to a variable and it can be
  // initialized or modified at any phase of the request, so set the
  // span operation name at request exit phase, which will take the latest
  // value of the variable pointed to opentracing_operation_name directive
  auto core_loc_conf = static_cast<ngx_http_core_loc_conf_t *>(
      ngx_http_get_module_loc_conf(request_, ngx_http_core_module));
  request_span_->SetOperationName(
      get_request_operation_name(request_, core_loc_conf, loc_conf_));

  // TODO: hack
  std::cout << "<><><><><><><><> about to run response script." << std::endl;
  std::cout << "[][][][][][][][][][][][] response script result: " << to_string(loc_conf_->response_info_script.run(request_)) << std::endl;
  // end TODO

  request_span_->Finish({ot::FinishTimestamp{finish_timestamp}});
}

// Expands the active span context into a list of key-value pairs and returns
// the value for `key` if it exists.
//
// Note: there's caching so that if lookup_span_context_value is repeatedly
// called for the same active span context, it will only be expanded once.
//
// See propagate_datadog_context
ngx_str_t RequestTracing::lookup_span_context_value(
    string_view key) {
  return span_context_querier_.lookup_value(request_, active_span(), key);
}

ngx_str_t RequestTracing::get_binary_context() const {
  const auto &span = active_span();
  std::ostringstream oss;
  auto was_successful = span.tracer().Inject(span.context(), oss);
  if (!was_successful) {
    throw std::runtime_error{was_successful.error().message()};
  }
  return to_ngx_str(request_->pool, oss.str());
}
}  // namespace nginx
}  // namespace datadog
