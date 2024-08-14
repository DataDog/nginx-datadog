#include "context.h"

#include <ngx_core.h>
#include <ngx_http_core_module.h>
#include <ngx_log.h>

#include <atomic>
#include <charconv>
#include <optional>
#include <sstream>
#include <stdexcept>
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
#include "header_tags.h"
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
                  dd::Span &span,
                  std::vector<dnsec::OwnedDdwafResult> &results) {
  if (results.empty()) {
    return;
  }

  seg.override_sampling_priority(2);  // USER-KEEP
  span.set_tag("appsec.event"sv, "true");

  rapidjson::StringBuffer buffer;
  JsonWriter w(buffer);
  w.StartObject();
  w.ConstLiteralKey("triggers"sv);

  w.StartArray();
  for (auto &&result : results) {
    auto events = dnsec::ddwaf_arr_obj{(*result).events};
    for (auto &&evt : events) {
      ddwaf_object_to_json(w, evt);
    }
  }
  w.EndArray(results.size());

  w.EndObject(1);
  w.Flush();

  std::string_view const json{buffer.GetString(), buffer.GetLength()};

  ngx_str_t json_ns{dnsec::ngx_stringv(json)};
  ngx_log_error(NGX_LOG_INFO, req.connection->log, 0, "appsec event: %V",
                &json_ns);

  span.set_tag("_dd.appsec.json"sv, json);
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

template <
    typename Callable, typename Ret = decltype(std::declval<Callable>()()),
    typename DefType =  // can't have void arguments. Have a dummy parameter for
                        // this case to avoid having to write a specialization
    std::conditional_t<std::is_same_v<Ret, void>, std::nullptr_t, Ret>>
auto catch_exceptions(std::string_view name, const ngx_http_request_t &req,
                      Callable &&f, DefType err_ret = {}) noexcept -> Ret {
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
}  // namespace

namespace datadog::nginx::security {

Context::Context(std::shared_ptr<OwnedDdwafHandle> handle)
    : waf_handle_{std::move(handle)}, stage_{new std::atomic<stage>{}} {
  if (!waf_handle_) {
    return;
  }

  ddwaf_handle ddwaf_h = waf_handle_->get();
  ctx_ = ddwaf_context_init(ddwaf_h);

  stage_->store(stage::START, std::memory_order_relaxed);
}

std::unique_ptr<Context> Context::maybe_create() {
  std::shared_ptr<OwnedDdwafHandle> handle = Library::get_handle();
  if (!handle) {
    return {};
  }
  return std::unique_ptr<Context>{new Context{std::move(handle)}};
}

template <typename Self>
class PolTaskCtx {
  PolTaskCtx(ngx_http_request_t &req, Context &ctx, dd::Span &span)
      : req_{req},
        ctx_{ctx},
        span_{span},
        prev_read_evt_handler_(req.read_event_handler),
        prev_write_evt_handler_(req.write_event_handler) {}

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

  bool submit(ngx_thread_pool_t *pool) noexcept {
    replace_handlers();

    req_.main->count++;

    if (ngx_thread_task_post(pool, &get_task()) != NGX_OK) {
      ngx_log_error(NGX_LOG_ERR, req_.connection->log, 0,
                    "failed to post task");

      req_.main->count--;
      restore_handlers();

      static_cast<Self *>(this)->~Self();
      return false;
    }

    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, req_.connection->log, 0,
                   "refcount after task submit: %d", req_.main->count);

    return true;
  }

 private:
  ngx_thread_task_t &get_task() noexcept {
    // ngx_thread_task_alloc allocates space for the context right after the
    // ngx_thread_task_t structure
    // NOLINTBEGIN(cppcoreguidelines-pro-type-reinterpret-cast)
    return *reinterpret_cast<ngx_thread_task_t *>(
        reinterpret_cast<char *>(this) - sizeof(ngx_thread_task_t));
    // NOLINTEND(cppcoreguidelines-pro-type-reinterpret-cast)
  }

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
    self->completion_handler_impl();
  }

  void completion_handler_impl() noexcept {
    restore_handlers();

    auto count = req_.main->count;
    if (count > 1) {
      // ngx_del_event(connection->read, NGX_READ_EVENT, 0) may've been called
      // by ngx_http_block_reading
      if (ngx_handle_read_event(req_.connection->read, 0) != NGX_OK) {
        ngx_http_finalize_request(&req_, NGX_HTTP_INTERNAL_SERVER_ERROR);
        ngx_log_error(NGX_LOG_ERR, req_.connection->log, 0,
                      "failed to re-enable read event");
      } else {
        req_.main->count--;
        static_cast<Self *>(this)->complete();
      }
    } else {
      ngx_log_debug0(
          NGX_LOG_DEBUG_HTTP, req_.connection->log, 0,
          "skipping run of completion handler because we're the only "
          "reference to the request; finalizing instead");
      ngx_http_finalize_request(&req_, NGX_DONE);
    }

    static_cast<Self *>(this)->~Self();
  }

  // define in subclasses
  // void complete() noexcept {}

  void replace_handlers() noexcept {
    req_.read_event_handler = ngx_http_block_reading;
    req_.write_event_handler = PolTaskCtx<Self>::empty_write_handler;
  }

  void restore_handlers() noexcept {
    req_.read_event_handler = prev_read_evt_handler_;
    req_.write_event_handler = prev_write_evt_handler_;
  }

  static void empty_write_handler(ngx_http_request_t *req) {
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, req->connection->log, 0,
                   "task wait empty handler");

    ngx_event_t *wev = req->connection->write;

    if (ngx_handle_write_event(wev, 0) != NGX_OK) {
      ngx_http_finalize_request(req, NGX_HTTP_INTERNAL_SERVER_ERROR);
    }
  }

 private:
  friend Self;
  ngx_http_request_t &req_;
  Context &ctx_;
  dd::Span &span_;
  std::optional<BlockSpecification> block_spec_;
  ngx_http_event_handler_pt prev_read_evt_handler_;
  ngx_http_event_handler_pt prev_write_evt_handler_;
  std::atomic<bool> ran_on_thread_{false};
};

class Pol1stWafCtx : public PolTaskCtx<Pol1stWafCtx> {
  using PolTaskCtx::PolTaskCtx;

  std::optional<BlockSpecification> do_handle(ngx_log_t &log) {
    return ctx_.run_waf_start(req_, span_);
  }

  void complete() noexcept {
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, req_.connection->log, 0,
                   "completion handler of waf start task");
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

  stage st = stage_->load(std::memory_order_relaxed);
  if (st != stage::START) {
    ngx_log_error(NGX_LOG_ERR, request.connection->log, 0,
                  "WAF context is not in the start stage");
    return false;
  }

  if (!stage_->compare_exchange_strong(st, stage::ENTERED_ON_START,
                                       std::memory_order_release,
                                       std::memory_order_relaxed)) {
    ngx_log_error(NGX_LOG_ERR, request.connection->log, 0,
                  "Unexpected concurrent change of stage_");
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

  if (task_ctx.submit(conf->waf_pool)) {
    ngx_log_debug(NGX_LOG_DEBUG_HTTP, request.connection->log, 0,
                  "posted initial waf task");
    return true;
  }
  return false;
}

namespace {

class Action {
 public:
  enum class type : unsigned char {
    BLOCK_REQUEST,
    REDIRECT_REQUEST,
    GENERATE_STACK,
    GENERATE_SCHEMA,
    UNKNOWN,
  };

  Action(ddwaf_map_obj action) : action_{action} {}

  auto type() const {
    auto key = action_.key();
    if (key == "block_request"sv) {
      return type::BLOCK_REQUEST;
    } else if (key == "redirect_request"sv) {
      return type::REDIRECT_REQUEST;
    } else if (key == "generate_stack"sv) {
      return type::GENERATE_STACK;
    } else if (key == "generate_schema"sv) {
      return type::GENERATE_SCHEMA;
    } else {
      return type::UNKNOWN;
    }
  }

  auto raw_type() const { return action_.key(); }

  int get_int_param(std::string_view k) const {
    ddwaf_obj v = action_.get(k);

    if (v.is_numeric()) {
      return v.numeric_val<int>();
    }

    if (!v.is_string()) {
      throw std::runtime_error{"expected numeric value for action parameter " +
                               std::string{k}};
    }

    // try to convert to number
    std::string_view const sv{v.string_val_unchecked()};
    int n;
    auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), n);
    if (ec == std::errc{} && ptr == sv.data() + sv.size()) {
      return n;
    }
    throw std::runtime_error{"expected numeric value for action parameter " +
                             std::string{k} + ", got " + std::string{sv}};
  }

  std::string_view get_string_param(std::string_view k) const {
    ddwaf_obj v = action_.get(k);

    if (v.is_string()) {
      return v.string_val_unchecked();
    }

    throw std::runtime_error{"expected string value for action parameter " +
                             std::string{k}};
  }

 private:
  ddwaf_map_obj action_;
};

class ActionsResult {
 public:
  ActionsResult(ddwaf_map_obj actions) : actions_{actions} {}

  class Iterator {
   public:
    using difference_type = ddwaf_obj::nb_entries_t;      // NOLINT
    using value_type = Action;                            // NOLINT
    using pointer = value_type *;                         // NOLINT
    using reference = value_type &;                       // NOLINT
    using iterator_category = std::forward_iterator_tag;  // NOLINT

    Iterator(ddwaf_map_obj actions, ddwaf_obj::nb_entries_t i)
        : actions_{actions}, i_{i} {}
    Iterator &operator++() {
      ++i_;
      return *this;
    }

    bool operator!=(const Iterator &other) const { return i_ != other.i_; }

    Action operator*() const {
      return Action{actions_.at_unchecked<ddwaf_map_obj>(i_)};
    }

   private:
    ddwaf_map_obj actions_;
    ddwaf_obj::nb_entries_t i_;
  };

  Iterator begin() const { return Iterator{ddwaf_map_obj{actions_}, 0}; }
  Iterator end() const {
    return Iterator{ddwaf_map_obj{actions_}, actions_.size()};
  }

 private:
  ddwaf_map_obj actions_;
};

BlockSpecification create_block_request_action(const Action &action) {
  enum BlockSpecification::ContentType ct{
      BlockSpecification::ContentType::AUTO};
  int status = action.get_int_param("status_code"sv);

  std::string_view const ct_sv = action.get_string_param("type"sv);
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

BlockSpecification create_redirect_request_action(const Action &action) {
  int status = action.get_int_param("status_code"sv);
  std::string_view const loc = action.get_string_param("location"sv);
  return {status, BlockSpecification::ContentType::NONE, loc};
}

std::optional<BlockSpecification> resolve_block_spec(
    const ActionsResult &actions, ngx_log_t &log) {
  for (Action act : actions) {
    auto type = act.type();

    if (type == Action::type::UNKNOWN) {
      std::string_view raw_type = act.raw_type();
      ngx_str_t raw_type_ns{dnsec::ngx_stringv(raw_type)};
      ngx_log_error(NGX_LOG_WARN, &log, 0,
                    "WAF indicated action %V, but such action id is unknown",
                    &raw_type_ns);
      continue;
    }

    if (type == Action::type::GENERATE_STACK ||
        type == Action::type::GENERATE_SCHEMA) {
      std::string_view raw_type = act.raw_type();
      ngx_str_t raw_type_ns{dnsec::ngx_stringv(raw_type)};
      ngx_log_error(NGX_LOG_NOTICE, &log, 0,
                    "WAF indicated action %V, but this action is "
                    "not supported. Ignoring.",
                    &raw_type_ns);
      continue;
    }

    if (type == Action::type::BLOCK_REQUEST) {
      return {create_block_request_action(act)};
    }

    if (type == Action::type::REDIRECT_REQUEST) {
      return {create_redirect_request_action(act)};
    }
  }

  return std::nullopt;
}
}  // namespace

std::optional<BlockSpecification> Context::run_waf_start(
    ngx_http_request_t &req, dd::Span &span) {
  auto st = stage_->load(std::memory_order_acquire);
  if (st != stage::ENTERED_ON_START) {
    return std::nullopt;
  }

  span.set_metric("_dd.appsec.enabled"sv, 1.0);
  span.set_tag("_dd.runtime_family", "cpp"sv);
  static const std::string_view libddwaf_version{ddwaf_get_version()};
  span.set_tag("_dd.appsec.waf.version", libddwaf_version);

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
  ddwaf_map_obj actions_arr{result.actions};
  if (code == DDWAF_MATCH && !actions_arr.empty()) {
    ActionsResult actions_res{actions_arr};
    block_spec = resolve_block_spec(actions_arr, *req.connection->log);
  }

  if (block_spec) {
    stage_->store(stage::AFTER_BEGIN_WAF_BLOCK, std::memory_order_release);
  } else {
    stage_->store(stage::AFTER_BEGIN_WAF, std::memory_order_release);
  }

  return block_spec;
}

ngx_int_t Context::output_body_filter(ngx_http_request_t &request,
                                      ngx_chain_t *chain,
                                      dd::Span &span) noexcept {
  return catch_exceptions(
      "output_body_filter"sv, request,
      [&]() { return Context::do_output_body_filter(request, chain, span); },
      static_cast<ngx_int_t>(NGX_ERROR));
}

class PolFinalWafCtx : public PolTaskCtx<PolFinalWafCtx> {
  using PolTaskCtx::PolTaskCtx;

  std::optional<BlockSpecification> do_handle(ngx_log_t &log) {
    return ctx_.run_waf_end(req_, span_);
  }

  void complete() noexcept {
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, req_.connection->log, 0,
                   "completion handler of waf task");
  }

  friend PolTaskCtx;
};

ngx_int_t Context::do_output_body_filter(ngx_http_request_t &request,
                                         ngx_chain_t *chain, dd::Span &span) {
  auto st = stage_->load(std::memory_order_acquire);
  if (st != stage::AFTER_BEGIN_WAF) {
    return ngx_http_next_output_body_filter(&request, chain);
  }

  PolFinalWafCtx &task_ctx = PolFinalWafCtx::create(request, *this, span);

  auto *conf = static_cast<datadog_loc_conf_t *>(
      ngx_http_get_module_loc_conf(&request, ngx_http_datadog_module));

  stage_->store(stage::BEFORE_RUN_WAF_END, std::memory_order_release);

  if (task_ctx.submit(conf->waf_pool)) {
    ngx_log_debug(NGX_LOG_DEBUG_HTTP, request.connection->log, 0,
                  "posted waf end task");
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

  return ngx_http_next_output_body_filter(&request, chain);
}

std::optional<BlockSpecification> Context::run_waf_end(
    ngx_http_request_t &request, dd::Span &span) {
  auto st = stage_->load(std::memory_order_acquire);
  if (st != stage::BEFORE_RUN_WAF_END) {
    return std::nullopt;
  }

  ddwaf_object *resp_data = collect_response_data(request, memres_);

  ddwaf_result result;
  DDWAF_RET_CODE const code = ddwaf_run(ctx_.resource, resp_data, nullptr,
                                        &result, Library::waf_timeout());
  if (code == DDWAF_MATCH) {
    results_.emplace_back(result);
  } else {
    ddwaf_result_free(&result);
  }

  stage_->store(stage::AFTER_RUN_WAF_END, std::memory_order_release);

  return std::nullopt;  // we don't support blocking in the final waf run
}

void Context::on_main_log_request(ngx_http_request_t &request,
                                  dd::Span &span) noexcept {
  catch_exceptions("on_log_request"sv, request, [&]() {
    return Context::do_on_main_log_request(request, span);
  });
}

void Context::do_on_main_log_request(ngx_http_request_t &request,
                                     dd::Span &span) {
  auto st = stage_->load(std::memory_order_acquire);
  if (st != stage::AFTER_RUN_WAF_END && st != stage::AFTER_BEGIN_WAF_BLOCK) {
    return;
  }

  set_header_tags(has_matches(), request, span);
  report_matches(request, span);
}

bool Context::has_matches() const noexcept { return !results_.empty(); }

void Context::report_matches(ngx_http_request_t &request, dd::Span &span) {
  if (results_.empty()) {
    return;
  }

  report_match(request, span.trace_segment(), span, results_);
  results_.clear();
}

}  // namespace datadog::nginx::security
