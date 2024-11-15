#include "context.h"

#include <ngx_core.h>
#include <ngx_event_posted.h>
#include <ngx_http_core_module.h>
#include <ngx_log.h>

#include <atomic>
#include <charconv>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

#include "../datadog_conf.h"
#include "../datadog_handler.h"
#include "../ngx_http_datadog_module.h"
#include "blocking.h"
#include "body_parse/body_parsing.h"
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
    : stage_{new std::atomic<stage>{}}, waf_handle_{std::move(handle)} {
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
        prev_read_evt_handler_{req.read_event_handler},
        prev_write_evt_handler_{req.write_event_handler} {}

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
                   "completion handler of waf start task: start");

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

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, req_.connection->log, 0,
                   "completion handler of waf start task: finish");
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
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, request.connection->log, 0,
                   "WAF context is not in the start stage. Internal redirect?");
    return false;
  }

  auto *conf = static_cast<datadog_loc_conf_t *>(
      ngx_http_get_module_loc_conf(&request, ngx_http_datadog_module));

  if (conf->waf_pool == nullptr) {
    ngx_log_debug(NGX_LOG_DEBUG_HTTP, request.connection->log, 0,
                  "no waf pool name defined for this location (uri: %V)",
                  &request.uri);
    transition_to_stage(stage::DISABLED);
    return false;
  }

  if (!stage_->compare_exchange_strong(st, stage::ENTERED_ON_START,
                                       std::memory_order_release,
                                       std::memory_order_relaxed)) {
    ngx_log_error(NGX_LOG_ERR, request.connection->log, 0,
                  "Unexpected concurrent change of stage_");
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

  std::optional<BlockSpecification> block_spec;
  if (code == DDWAF_MATCH) {
    results_.emplace_back(result);
    ddwaf_map_obj actions_arr{result.actions};
    if (!actions_arr.empty()) {
      ActionsResult actions_res{actions_arr};
      block_spec = resolve_block_spec(actions_arr, *req.connection->log);
    }
  } else {
    ddwaf_result_free(&result);
  }

  if (block_spec) {
    stage_->store(stage::AFTER_BEGIN_WAF_BLOCK, std::memory_order_release);
  } else {
    stage_->store(stage::AFTER_BEGIN_WAF, std::memory_order_release);
  }

  return block_spec;
}

ngx_int_t Context::request_body_filter(ngx_http_request_t &request,
                                       ngx_chain_t *chain,
                                       dd::Span &span) noexcept {
  return catch_exceptions(
      "request_body_filter"sv, request,
      [&]() { return Context::do_request_body_filter(request, chain, span); },
      static_cast<ngx_int_t>(NGX_ERROR));
}

ngx_int_t Context::output_body_filter(ngx_http_request_t &request,
                                      ngx_chain_t *chain,
                                      dd::Span &span) noexcept {
  return catch_exceptions(
      "output_body_filter"sv, request,
      [&]() { return Context::do_output_body_filter(request, chain, span); },
      static_cast<ngx_int_t>(NGX_ERROR));
}

class PolReqBodyWafCtx : public PolTaskCtx<PolReqBodyWafCtx> {
  using PolTaskCtx::PolTaskCtx;

  std::optional<BlockSpecification> do_handle(ngx_log_t &log) {
    return ctx_.run_waf_req_post(req_, span_);
  }

  void complete() noexcept {
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, req_.connection->log, 0,
                   "completion handler of waf req post task: start");
    bool const ran = ran_on_thread_.load(std::memory_order_acquire);

    if (ran && block_spec_) {
      span_.set_tag("appsec.blocked"sv, "true"sv);
      ctx_.waf_req_post_done(req_, true);

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
      ctx_.waf_req_post_done(req_, false);
      ngx_log_debug(NGX_LOG_DEBUG_HTTP, req_.connection->log, 0,
                    "not blocking after request body waf run; "
                    "triggering read event on connection");
      ngx_post_event(req_.connection->read, &ngx_posted_events);
    }

    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, req_.connection->log, 0,
                   "completion handler of waf req post task: finish");
  }

  friend PolTaskCtx;
};

class PolFinalWafCtx : public PolTaskCtx<PolFinalWafCtx> {
  using PolTaskCtx::PolTaskCtx;

  std::optional<BlockSpecification> do_handle(ngx_log_t &log) {
    return ctx_.run_waf_end(req_, span_);
  }

  void complete() noexcept {
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, req_.connection->log, 0,
                   "completion of final waf task");
  }

  friend PolTaskCtx;
};

ngx_int_t Context::do_request_body_filter(ngx_http_request_t &request,
                                          ngx_chain_t *in, dd::Span &span) {
  auto st = stage_->load(std::memory_order_acquire);
  ngx_log_debug3(
      NGX_LOG_DEBUG_HTTP, request.connection->log, 0,
      "request body filter %s in chain. Current data: %lu, Stage: %d",
      in ? "with" : "without", filter_ctx_.out_total, st);

  if (st == stage::AFTER_BEGIN_WAF) {
    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, request.connection->log, 0,
                   "first filter call, req refcount=%d", request.main->count);
    // buffer the request body during reading
    // https://github.com/nginx/nginx/commit/67d160bf25e02ba6679bb6c3b9cbdfeb29b759de
    // https://nginx.org/en/docs/dev/development_guide.html#http_request_body_filters
    // this essentially avoids early req termination if buffers are not read
    // (position advanced) by the filters. However, nginx doesn't keep calling
    // the filters in that case. We use it to avoid synchronous calls to the
    // filter after we collected enough data to call the WAF. Asynchronous calls
    // are avoided by swapping the handlers before starting the WAF task.
    request.request_body->filter_need_buffering = true;

    if (in && in->buf->pos ==
                  request.header_in->pos - (in->buf->last - in->buf->pos)) {
      // preread call by ngx_http_read_client_request_body.
      // read and write handlers were not set yet, so don't launch the
      // waf now. We'll do it on the next call.
      ngx_log_debug0(NGX_LOG_DEBUG_HTTP, request.connection->log, 0,
                     "1st preread call, no waf task submission yet");
      st = transition_to_stage(stage::COLLECTING_ON_REQ_DATA_PREREAD);
    } else {
      st = transition_to_stage(stage::COLLECTING_ON_REQ_DATA);
    }
  }

  if (st == stage::COLLECTING_ON_REQ_DATA_PREREAD) {
    // we're guaranteed to be called again synchronously, so we shouldn't
    // call the WAF at this point

    if (buffer_chain(*request.pool, in, true) != NGX_OK) {
      return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    st = transition_to_stage(stage::COLLECTING_ON_REQ_DATA);
  } else if (st == stage::COLLECTING_ON_REQ_DATA) {
    // check if we have enough data to run the WAF
    size_t new_size = filter_ctx_.out_total;
    bool is_last = filter_ctx_.found_last;
    for (auto *cl = in; cl; cl = cl->next) {
      new_size += cl->buf->last - cl->buf->pos;
      is_last = is_last || cl->buf->last_buf;
    }

    bool run_waf = is_last || new_size >= kMaxFilterData;
    if (run_waf) {
      // do not consume the buffer so that this filter is not called again
      if (buffer_chain(*request.pool, in, false) != NGX_OK) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
      }

      if (filter_ctx_.out_total == 0) {
        ngx_log_debug0(NGX_LOG_DEBUG_HTTP, request.connection->log, 0,
                       "no data to run WAF on");
        transition_to_stage(stage::AFTER_ON_REQ_WAF);
        goto pass_downstream;
      }

      ngx_log_debug2(NGX_LOG_DEBUG_HTTP, request.connection->log, 0,
                     "running WAF on %lu bytes of data (found last: %s)",
                     new_size, is_last ? "true" : "false");

      PolReqBodyWafCtx &task_ctx =
          PolReqBodyWafCtx::create(request, *this, span);

      transition_to_stage(stage::SUSPENDED_ON_REQ_WAF);

      auto *conf = static_cast<datadog_loc_conf_t *>(
          ngx_http_get_module_loc_conf(&request, ngx_http_datadog_module));

      if (task_ctx.submit(conf->waf_pool)) {
        ngx_log_debug(NGX_LOG_DEBUG_HTTP, request.connection->log, 0,
                      "posted request body waf req post task");
      } else {
        transition_to_stage(stage::AFTER_ON_REQ_WAF);
        ngx_log_error(NGX_LOG_NOTICE, request.connection->log, 0,
                      "posted request body waf req post task failed. Passing "
                      "data to downstream filters immediately");
        goto pass_downstream;
      }
    } else {  // !run_waf; we need more data
      if (buffer_chain(*request.pool, in, true) != NGX_OK) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
      }
    }
  } else if (st == stage::AFTER_ON_REQ_WAF ||
             st == stage::AFTER_ON_REQ_WAF_BLOCK) {
    if (filter_ctx_.out) {  // first call after WAF ended
      ngx_log_debug1(NGX_LOG_DEBUG_HTTP, request.connection->log, 0,
                     "first filter call after WAF ended, req refcount=%d",
                     request.main->count);
      if (buffer_chain(*request.pool, in, false) != NGX_OK) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
      }

    pass_downstream:
      // pass saved buffers downstream
      auto rc = ngx_http_next_request_body_filter(&request, filter_ctx_.out);

      filter_ctx_.clear(*request.pool);

      for (auto *cl = filter_ctx_.out; cl;) {
        auto *ln = cl;
        cl = cl->next;
        ngx_free_chain(request.pool, ln);
      }
      filter_ctx_.out = nullptr;
      filter_ctx_.out_last = &filter_ctx_.out;

      return rc;
    }

    return ngx_http_next_request_body_filter(&request, in);
  } else if (st == stage::SUSPENDED_ON_REQ_WAF) {
    if (in) {
      ngx_log_error(
          NGX_LOG_NOTICE, request.connection->log, 0,
          "unexpected filter call with data in stage SUSPENDED_ON_REQ_WAF");
      // we're in a suspended state, so we don't expect to be called
      // if we are called, it's a bit troubling, because the write that happens
      // in the buffered chain in the next statement is not synchronized with
      // the read that happens in the WAF thread.
      if (buffer_chain(*request.pool, in, false) != NGX_OK) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
      }
    }
  } else {
    if (st != stage::DISABLED            /* no WAF for this request */
        && st != stage::ENTERED_ON_START /* could not submit 1st WAF task */
        && st != stage::AFTER_BEGIN_WAF_BLOCK /* blocked by 1st WAF task */) {
      ngx_log_error(NGX_LOG_NOTICE, request.connection->log, 0,
                    "unexpected filter call in stage %d", st);
    }
    return ngx_http_next_request_body_filter(&request, in);
  }

  // we continue to call the next filter, but don't pass any data
  return ngx_http_next_request_body_filter(&request, nullptr);
}

ngx_int_t Context::buffer_chain(ngx_pool_t &pool, ngx_chain_t *in,
                                bool consume) {
  for (auto *in_ch = in; in_ch; in_ch = in_ch->next) {
    ngx_chain_t *new_ch = ngx_alloc_chain_link(&pool);  // uninitialized
    if (!new_ch) {
      return NGX_ERROR;
    }

    auto *buf = in_ch->buf;
    auto size = buf->last - buf->pos;
    if (consume || buf->recycled) {  // copy the buffer and consume the original
      ngx_buf_t *new_buf = ngx_create_temp_buf(&pool, size);
      if (!new_buf) {
        return NGX_ERROR;
      }

      new_buf->tag = &ngx_http_datadog_module;

      new_buf->last = ngx_copy(new_buf->pos, buf->pos, size);
      buf->pos = buf->last;
      new_buf->last_buf = buf->last_buf;
      new_buf->tag = buf->tag;

      new_ch->buf = new_buf;
    } else {
      new_ch->buf = buf;
    }
    new_ch->next = nullptr;

    filter_ctx_.out_total += size;
    if (buf->last_buf) {
      filter_ctx_.found_last = true;
    }

    *filter_ctx_.out_last = new_ch;
    filter_ctx_.out_last = &new_ch->next;
  }

  return NGX_OK;
}

void Context::FilterCtx::clear(ngx_pool_t &pool) noexcept {
  for (ngx_chain_t *cl = out; cl;) {
    ngx_chain_t *ln = cl;
    cl = cl->next;
    ngx_free_chain(&pool, ln);
  }
  out = nullptr;
  out_last = &out;
}

ngx_int_t Context::do_output_body_filter(ngx_http_request_t &request,
                                         ngx_chain_t *chain, dd::Span &span) {
  auto st = stage_->load(std::memory_order_acquire);
  if (st != stage::AFTER_BEGIN_WAF && st != stage::AFTER_ON_REQ_WAF) {
    return ngx_http_next_output_body_filter(&request, chain);
  }

  PolFinalWafCtx &task_ctx = PolFinalWafCtx::create(request, *this, span);

  auto *conf = static_cast<datadog_loc_conf_t *>(
      ngx_http_get_module_loc_conf(&request, ngx_http_datadog_module));

  transition_to_stage(stage::BEFORE_RUN_WAF_END);

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

std::optional<BlockSpecification> Context::run_waf_req_post(
    ngx_http_request_t &request, dd::Span &span) {
  ddwaf_obj input;
  ddwaf_map_obj &input_map = input.make_map(1, memres_);
  ddwaf_obj &entry = input_map.at_unchecked(0);
  entry.set_key("server.request.body"sv);

  bool success = parse_body(entry, request, *filter_ctx_.out,
                            filter_ctx_.out_total, memres_);

  if (!success) {
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, request.connection->log, 0,
                   "failed to parse request body for WAF");
    return std::nullopt;
  }

  ddwaf_result result;
  DDWAF_RET_CODE const code = ddwaf_run(ctx_.resource, &input, nullptr, &result,
                                        Library::waf_timeout());
  if (code != DDWAF_MATCH) {
    ddwaf_result_free(&result);
    return std::nullopt;
  }

  results_.emplace_back(result);

  std::optional<BlockSpecification> block_spec;
  ddwaf_map_obj actions_arr{result.actions};
  if (!actions_arr.empty()) {
    ActionsResult actions_res{actions_arr};
    block_spec = resolve_block_spec(actions_arr, *request.connection->log);
  }

  return block_spec;
}

void Context::waf_req_post_done(ngx_http_request_t &request, bool blocked) {
  bool res = checked_transition_to_stage(
      stage::SUSPENDED_ON_REQ_WAF,
      blocked ? stage::AFTER_ON_REQ_WAF_BLOCK : stage::AFTER_ON_REQ_WAF);

  if (!res) {
    ngx_log_error(NGX_LOG_ERR, request.connection->log, 0,
                  "call to waf_req_post_done without current stage being "
                  "SUSPENDED_ON_REQ_WAF");
  }
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

  transition_to_stage(stage::AFTER_RUN_WAF_END);

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
  if (st != stage::AFTER_RUN_WAF_END && st != stage::AFTER_BEGIN_WAF_BLOCK &&
      st != stage::AFTER_ON_REQ_WAF_BLOCK) {
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
