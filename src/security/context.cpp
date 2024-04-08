#include "context.h"

#include <ngx_core.h>
#include <ngx_http_core_module.h>

#include <atomic>
#include <charconv>
#include <optional>
#include <sstream>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <variant>

#include "../datadog_conf.h"
#include "../datadog_context.h"
#include "../datadog_handler.h"
#include "../ngx_http_datadog_module.h"
#include "../tracing_library.h"
#include "blocking.h"
#include "collection.h"
#include "ddwaf_obj.h"
#include "library.h"
#include "util.h"

extern "C" {
#include <ngx_hash.h>
#include <ngx_http.h>
#include <ngx_regex.h>
#include <ngx_string.h>
#include <ngx_thread_pool.h>
}

#include <datadog/span.h>
#include <datadog/span_data.h>
#include <datadog/trace_segment.h>
#include <ddwaf.h>
#include <rapidjson/document.h>
#include <rapidjson/encodings.h>
#include <rapidjson/prettywriter.h>

using namespace std::literals;

namespace {

namespace dnsec = datadog::nginx::security;

class JsonWriter : public rapidjson::Writer<rapidjson::StringBuffer> {
  using rapidjson::Writer<rapidjson::StringBuffer>::Writer;

 public:
  // NOLINTNEXTLINE(readability-identifier-naming)
  bool ConstLiteralKey(std::string_view sv) {
    return String(sv.data(), sv.length(), false);
  }
};

void ddwaf_object_to_json(JsonWriter &w, const ddwaf_object &dobj);

void report_match(const ngx_http_request_t &req, dd::TraceSegment &seg,
                  dd::SpanData &span,
                  std::vector<dnsec::OwnedDdwafResult> &results) {
  static constexpr std::string_view appsec_event{"appsec.event"};
  static constexpr std::string_view appsec_json{"_dd.appsec.json"};

  if (results.empty()) {
    return;
  }

  seg.override_sampling_priority(2);  // USER-KEEP
  span.tags[std::string{appsec_event}] = "true";

  rapidjson::StringBuffer buffer;
  JsonWriter w(buffer);
  w.StartObject();
  w.ConstLiteralKey("triggers"sv);

  w.StartArray();
  for (auto &&result : results) {
    auto &&events = (*result).events;
    for (std::size_t i = 0; i < events.nbEntries; i++) {
      auto &&evt = events.array[i];
      ddwaf_object_to_json(w, evt);
    }
  }
  w.EndArray(results.size());

  w.EndObject(1);
  w.Flush();

  std::string_view const json{buffer.GetString(), buffer.GetLength()};

  ngx_str_t json_ns{dnsec::ngx_stringv(json)};
  ngx_log_error(NGX_LOG_WARN, req.connection->log, 0, "appsec event: %V",
                &json_ns);

  span.tags[std::string{appsec_json}] = json;
}

// NOLINTNEXTLINE(misc-no-recursion)
void ddwaf_object_to_json(JsonWriter &w, const ddwaf_object &dobj) {
  switch (dobj.type) {
    case DDWAF_OBJ_MAP:
      w.StartObject();
      for (std::size_t i = 0; i < dobj.nbEntries; i++) {
        auto &&e = dobj.array[i];
        w.Key(e.parameterName, e.parameterNameLength, false);
        ddwaf_object_to_json(w, e);
      }
      w.EndObject(dobj.nbEntries);
      break;
    case DDWAF_OBJ_ARRAY:
      w.StartArray();
      for (std::size_t i = 0; i < dobj.nbEntries; i++) {
        auto &&e = dobj.array[i];
        ddwaf_object_to_json(w, e);
      }
      w.EndArray(dobj.nbEntries);
      break;
    case DDWAF_OBJ_STRING:
      w.String(dobj.stringValue, dobj.nbEntries, false);
      break;
    case DDWAF_OBJ_SIGNED:
      w.Int64(dobj.intValue);
      break;
    case DDWAF_OBJ_UNSIGNED:
      w.Uint64(dobj.uintValue);
      break;
    case DDWAF_OBJ_FLOAT:
      w.Double(dobj.f64);
      break;
    case DDWAF_OBJ_BOOL:
      w.Bool(dobj.boolean);
      break;
    case DDWAF_OBJ_INVALID:
    case DDWAF_OBJ_NULL:
      w.Null();
      break;
  }
}

template <typename Callable,
          typename Ret = decltype(std::declval<Callable>()())>
auto catch_exceptions(std::string_view name, const ngx_http_request_t &req,
                      Callable &&f, Ret err_ret = {}) noexcept {
  try {
    return std::invoke(std::forward<Callable>(f));
  } catch (const std::exception &e) {
    ngx_log_error(NGX_LOG_ERR, req.connection->log, 0,
                  "security_context::%.*s: %s", static_cast<int>(name.size()),
                  name.data(), e.what());
  } catch (...) {
    ngx_log_error(NGX_LOG_ERR, req.connection->log, 0,
                  "security_context::%.*s: unknown exception",
                  static_cast<int>(name.size()), name.data());
  }
  if constexpr (!std::is_same_v<Ret, void>) {
    return err_ret;
  }
}

dd::SpanData &get_span_data(dd::Span &s) {
  auto *p =  // NOLINTNEXTLINE
      reinterpret_cast<char *>(&s) + sizeof(std::shared_ptr<dd::TraceSegment>);
  dd::SpanData *span_data;
  std::memcpy(&span_data, p, sizeof(span_data));
  if (span_data == nullptr) {
    throw std::runtime_error("get_span_data failed: could not find span data");
  }
  return *span_data;
}
}  // namespace

namespace datadog::nginx::security {

Context::Context(std::shared_ptr<WafHandle> handle)
    : waf_handle_{std::move(handle)}, stage_{new std::atomic<stage>{}} {
  if (!waf_handle_) {
    return;
  }

  ddwaf_handle ddwaf_h = waf_handle_->get();
  ctx_ = ddwaf_context_init(ddwaf_h);

  stage_->store(stage::START, std::memory_order_release);
}

std::unique_ptr<Context> Context::maybe_create() {
  std::shared_ptr<WafHandle> handle = Library::get_handle();
  if (!handle) {
    return {};
  }
  return std::unique_ptr<Context>{new Context{std::move(handle)}};
}

template <typename Self>
class PolTaskCtx {
  PolTaskCtx(ngx_http_request_t &req, Context &ctx, dd::Span &span)
      : req_{req}, ctx_{ctx}, span_{span} {}

 public:
  // the returned reference is request pool allocated and must have its
  // destructor explicitly called if not submitted
  template <typename... Args>
  static Self &create(ngx_http_request_t &req, Context &ctx, dd::Span &span,
                      Args &&...extra_args) {
    ngx_thread_task_t *task = ngx_thread_task_alloc(req.pool, sizeof(Self));
    if (!task) {
      throw std::runtime_error{"failed to allocate task"};
    }

    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    auto *task_ctx =
        new (task->ctx) Self{req, ctx, span, std::forward<Args>(extra_args)...};
    task->handler = &PolTaskCtx::handler;
    task->event.handler = &PolTaskCtx::completion_handler;
    task->event.data = task_ctx;

    return *task_ctx;
  }

  ngx_thread_task_t &get_task() noexcept {
    // ngx_thread_task_alloc allocates space for the context right after the
    // ngx_thread_task_t structure
    // NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast)
    return *reinterpret_cast<ngx_thread_task_t *>(
        reinterpret_cast<char *>(this) - sizeof(ngx_thread_task_t));
    // NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast)
  }

 private:
  static void handler(void *self, ngx_log_t *log) noexcept {
    static_cast<Self *>(self)->handle(log);
  }

  // runs on the thread pool
  void handle(ngx_log_t *log) noexcept {
    try {
      ngx_log_debug(NGX_LOG_DEBUG_HTTP, req_.connection->log, 0,
                    "before task main: %p", &req_);
      block_spec_ = static_cast<Self *>(this)->do_handle(*log);
      ngx_log_debug(NGX_LOG_DEBUG_HTTP, req_.connection->log, 0,
                    "after task main: %p", &req_);
      ran_on_thread_.store(true, std::memory_order_release);
    } catch (std::exception &e) {
      ngx_log_error(NGX_LOG_ERR, log, 0, "task failed: %s", e.what());
    } catch (...) {
      ngx_log_error(NGX_LOG_ERR, log, 0, "task failed: unknown failure");
    }
  }

  // define in subclasses
  // std::optional<block_spec> do_handle(ngx_log_t &log) {}

  // runs on the main thread
  static void completion_handler(ngx_event_t *evt) noexcept {
    auto *self = static_cast<Self *>(evt->data);
    self->complete();
    self->~PolTaskCtx();
  }

  // define in subclasses
  // void complete() noexcept {}

 private:
  friend Self;
  ngx_http_request_t &req_;
  Context &ctx_;
  dd::Span &span_;
  std::optional<BlockSpecification> block_spec_;
  std::atomic<bool> ran_on_thread_{false};
};

class Pol1stWafCtx : public PolTaskCtx<Pol1stWafCtx> {
  using PolTaskCtx::PolTaskCtx;

  std::optional<BlockSpecification> do_handle(ngx_log_t &log) {
    return ctx_.run_waf_start(req_, span_);
  }

  void complete() noexcept {
    bool const ran = ran_on_thread_.load(std::memory_order_acquire);
    if (ran && block_spec_) {
      span_.set_tag("appsec.blocked"sv, "true"sv);

      auto *service = BlockingService::get_instance();
      assert(service != nullptr);
      try {
        service->block(*block_spec_, req_);
      } catch (const std::exception &e) {
        ngx_log_error(NGX_LOG_ERR, req_.connection->log, 0,
                      "failed to block request: %s", e.what());
        ngx_http_finalize_request(&req_, NGX_DONE);
      }
    } else {
      req_.phase_handler++;  // move past us
      ngx_http_core_run_phases(&req_);
    }
  }

  friend PolTaskCtx;
};

bool Context::on_request_start(ngx_http_request_t &request,
                               dd::Span &span) noexcept {
  return catch_exceptions("on_request_start"sv, request, [&]() {
    return Context::do_on_request_start(request, span);
  });
}

bool Context::do_on_request_start(ngx_http_request_t &request, dd::Span &span) {
  if (ctx_.resource == nullptr) {
    return false;
  }
  if (stage_->load(std::memory_order_acquire) != stage::START) {
    ngx_log_error(NGX_LOG_ERR, request.connection->log, 0,
                  "WAF context is not in the start stage");
    return false;
  }

  auto *conf = static_cast<datadog_loc_conf_t *>(
      ngx_http_get_module_loc_conf(&request, ngx_http_datadog_module));

  if (conf->waf_pool == nullptr) {
    ngx_log_debug(NGX_LOG_DEBUG_HTTP, request.connection->log, 0,
                  "no waf pool name defined for this location (uri: %V)",
                  &request.uri);
    return false;
  }

  auto &task_ctx = Pol1stWafCtx::create(request, *this, span);

  if (ngx_thread_task_post(conf->waf_pool, &task_ctx.get_task()) != NGX_OK) {
    // log error
    ngx_log_error(NGX_LOG_ERR, request.connection->log, 0,
                  "failed to post waf task");
    task_ctx.~Pol1stWafCtx();
    return false;
  }

  ngx_log_debug(NGX_LOG_DEBUG_HTTP, request.connection->log, 0,
                "posted waf task");
  return true;
}

namespace {
template <typename Map>
int get_or_default_int(const Map &m, std::string_view k, int def) {
  auto it = m.find(k);
  if (it == m.end()) {
    return def;
  }
  const ActionInfo::StrOrInt &v{it->second};
  if (std::holds_alternative<int>(v)) {
    return std::get<int>(v);
  }

  // try to convert to number
  std::string_view const sv{std::get<std::string>(v)};
  int n;
  auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), n);
  if (ec == std::errc{} && ptr == sv.data() + sv.size()) {
    return n;
  }
  return def;
}

template <typename Map>  // NOLINTNEXTLINE
std::string_view getOrDefaultString(const Map &m, std::string_view k,
                                    std::string_view def) {
  auto it = m.find(k);
  if (it == m.end()) {
    return def;
  }
  const ActionInfo::StrOrInt &v{it->second};
  if (std::holds_alternative<std::string>(v)) {
    return {std::get<std::string>(v)};
  }
  return def;
}

BlockSpecification create_block_request_action(const ActionInfo &ai) {
  static constexpr int default_status = 403;
  enum BlockSpecification::ContentType ct{
      BlockSpecification::ContentType::AUTO};
  int status =
      get_or_default_int(ai.parameters, "status_code"sv, default_status);
  if (status < 100 || status > 599) {
    status = default_status;
  }
  std::string_view const ct_sv =
      getOrDefaultString(ai.parameters, "type"sv, "auto"sv);
  if (ct_sv == "auto"sv) {
    ct = BlockSpecification::ContentType::AUTO;
  } else if (ct_sv == "html"sv) {
    ct = BlockSpecification::ContentType::HTML;
  } else if (ct_sv == "json"sv) {
    ct = BlockSpecification::ContentType::JSON;
  } else if (ct_sv == "none"sv) {
    ct = BlockSpecification::ContentType::NONE;
  }

  return BlockSpecification{status, ct};
}

std::optional<BlockSpecification> create_redirect_request_action(
    const ActionInfo &ai) {
  static constexpr int default_status = 303;
  int status =
      get_or_default_int(ai.parameters, "status_code"sv, default_status);
  if (status < 300 || status > 399) {
    status = default_status;
  }

  std::string_view const loc =
      getOrDefaultString(ai.parameters, "location"sv, ""sv);
  if (loc == ""sv) {
    // this is mandated by spec, strange as it might be
    return {{403, BlockSpecification::ContentType::AUTO}};
  }

  return {
      BlockSpecification{status, BlockSpecification::ContentType::NONE, loc}};
}

std::optional<BlockSpecification> resolve_block_spec(
    const WafHandle::action_info_map_t &aim, const ddwaf_arr_obj &actions_arr,
    ngx_log_t &log) {
  for (auto &&id_obj : actions_arr) {
    if (id_obj.type != DDWAF_OBJ_STRING) {
      continue;
    }

    std::string_view const id{id_obj.string_val_unchecked()};
    auto &&it = aim.find(id);
    if (it == aim.end()) {
      ngx_log_error(NGX_LOG_WARN, &log, 0,
                    "WAF indicated action %.*s, but such action id is unknown",
                    static_cast<int>(id.size()), id.data());
      continue;
    }

    const ActionInfo &ai = it->second;
    if (ai.type == "block_request"sv) {
      return {create_block_request_action(ai)};
    }
    if (ai.type == "redirect_request"sv) {
      auto maybe_spec = create_redirect_request_action(ai);
      if (maybe_spec) {
        return maybe_spec;
      }
      ngx_log_error(NGX_LOG_WARN, &log, 0,
                    "redirect_request action has no location");

    } else {
      ngx_log_error(NGX_LOG_WARN, &log, 0, "Action type %.*s is unknown",
                    static_cast<int>(ai.type.size()), ai.type.data());
      continue;
    }
  }
  return std::nullopt;
}
}  // namespace

std::optional<BlockSpecification> Context::run_waf_start(
    ngx_http_request_t &req, dd::Span &span) {
  auto st = stage_->load(std::memory_order_acquire);
  if (st != stage::START) {
    return std::nullopt;
  }

  dd::SpanData &span_data = get_span_data(span);

  span_data.numeric_tags.insert_or_assign("_dd.appsec.enabled", 1.0);

  ddwaf_object *data = collect_request_data(req, memres_);

  ddwaf_result result;
  auto code =
      ddwaf_run(ctx_.resource, data, nullptr, &result, Library::waf_timeout());
  if (code == DDWAF_MATCH) {
    results_.emplace_back(result);
  } else {
    ddwaf_result_free(&result);
  }

  std::optional<BlockSpecification> block_spec;
  ddwaf_arr_obj actions_arr{result.actions};
  if (code == DDWAF_MATCH && actions_arr.nbEntries > 0) {
    const WafHandle::action_info_map_t &aim = waf_handle_->action_info_map();
    block_spec = resolve_block_spec(aim, actions_arr, *req.connection->log);
  }

  if (block_spec) {
    stage_->store(stage::AFTER_BEGIN_WAF_BLOCK, std::memory_order_release);
  } else {
    stage_->store(stage::AFTER_BEGIN_WAF, std::memory_order_release);
  }

  return block_spec;
}

ngx_int_t Context::output_header_filter(ngx_http_request_t &request,
                                        dd::Span &span) noexcept {
  return catch_exceptions(
      "output_header_filter"sv, request,
      [&]() { return Context::do_output_header_filter(request, span); },
      static_cast<ngx_int_t>(NGX_ERROR));
}

class PolFinalWafCtx : public PolTaskCtx<PolFinalWafCtx> {
  using PolTaskCtx::PolTaskCtx;

  std::optional<BlockSpecification> do_handle(ngx_log_t &log) {
    return ctx_.run_waf_end(req_, span_);
  }

  void complete() noexcept {
    ran_on_thread_.load(std::memory_order_acquire);
    ngx_http_finalize_request(&req_, NGX_DONE);
  }

  friend PolTaskCtx;
};

ngx_int_t Context::do_output_header_filter(ngx_http_request_t &request,
                                           dd::Span &span) {
  auto st = stage_->load(std::memory_order_acquire);
  if (st != stage::AFTER_BEGIN_WAF) {
    return ngx_http_next_output_header_filter(&request);
  }

  PolFinalWafCtx &task_ctx = PolFinalWafCtx::create(request, *this, span);

  auto *conf = static_cast<datadog_loc_conf_t *>(
      ngx_http_get_module_loc_conf(&request, ngx_http_datadog_module));

  // so the request isn't freed after the filter
  request.count++;

  if (ngx_thread_task_post(conf->waf_pool, &task_ctx.get_task()) != NGX_OK) {
    // log error
    ngx_log_error(NGX_LOG_ERR, request.connection->log, 0,
                  "failed to post waf task");
    task_ctx.~PolFinalWafCtx();
    return NGX_ERROR;
  }

  // blocking not supported
  // I think supporting this would involve registering a body filter that
  // buffers the original request output while awaiting a response from the WAF
  // (see the postpone filter). The reason for this is that the there is no way
  // to suspend the request from the header filter. If we return something other
  // than NGX_OK from it, the caller of ngx_http_send_header() won't try to send
  // the body.

  // If we want to implement this in the future, this is the (untested) idea: we
  // need to suppress sending the header from our header filter, return NGX_OK,
  // enable caching the body from a body filter while the WAF is running, and
  // once we get a response from the WAF: a) if we got blocking response,
  // discard the buffered data and send our blocking response (headers
  // included), or b) otherwise, invoke the next header filter to write the
  // original header, send the cached body data, discard it and disable caching
  // body data.

  ngx_log_debug(NGX_LOG_DEBUG_HTTP, request.connection->log, 0,
                "posted waf end task");
  return ngx_http_next_output_header_filter(&request);
}

std::optional<BlockSpecification> Context::run_waf_end(
    ngx_http_request_t &request, dd::Span &span) {
  ddwaf_object *resp_data = collect_response_data(request, memres_);

  ddwaf_result result;
  DDWAF_RET_CODE const code = ddwaf_run(ctx_.resource, resp_data, nullptr,
                                        &result, Library::waf_timeout());
  if (code == DDWAF_MATCH) {
    results_.emplace_back(result);
  } else {
    ddwaf_result_free(&result);
  }

  if (!results_.empty()) {
    auto &span_data = get_span_data(span);
    report_match(request, span.trace_segment(), span_data, results_);
    results_.clear();
  }

  stage_->store(stage::AFTER_REPORT, std::memory_order_release);

  return std::nullopt;  // we don't support blocking in the final waf run
}

}  // namespace datadog::nginx::security
