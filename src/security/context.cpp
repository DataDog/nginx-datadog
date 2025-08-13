#include "context.h"

#include <atomic>
#include <climits>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include "../datadog_conf.h"
#include "../datadog_handler.h"
#include "../ngx_http_datadog_module.h"
#include "blocking.h"
#include "body_parse/body_parsing.h"
#include "client_ip.h"
#include "collection.h"
#include "datadog_context.h"
#include "ddwaf_obj.h"
#include "header_tags.h"
#include "library.h"
#include "stats.h"
#include "util.h"

extern "C" {
#include <ngx_buf.h>
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event_posted.h>
#include <ngx_files.h>
#include <ngx_hash.h>
#include <ngx_http.h>
#include <ngx_http_core_module.h>
#include <ngx_http_request.h>
#include <ngx_http_v2.h>
#include <ngx_log.h>
#include <ngx_regex.h>
#include <ngx_stream.h>
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

// Bit flags for testing ngx_thread_task_post failures
constexpr ngx_int_t kTaskPostFailureMaskInitialWaf = 1;
constexpr ngx_int_t kTaskPostFailureMaskReqBodyWaf = 2;
constexpr ngx_int_t kTaskPostFailureMaskFinalWaf = 4;

Context::Context(std::shared_ptr<OwnedDdwafHandle> handle,
                 bool apm_tracing_enabled)
    : stage_{new std::atomic<stage>{}},
      apm_tracing_enabled_{apm_tracing_enabled} {
  if (!handle) {
    return;
  }

  waf_ctx_ = std::make_unique<DdwafContext>(handle);

  stage_->store(stage::START, std::memory_order_relaxed);
  Stats::context_started();
}

Context::~Context() { Stats::context_closed(); }

std::unique_ptr<Context> Context::maybe_create(
    std::optional<std::size_t> max_saved_output_data,
    bool apm_tracing_enabled) {
  std::shared_ptr<OwnedDdwafHandle> handle = Library::get_handle();
  if (!handle) {
    return {};
  }
  auto res = std::unique_ptr<Context>{
      new Context{std::move(handle), apm_tracing_enabled}};
  if (max_saved_output_data) {
    res->max_saved_output_data_ = *max_saved_output_data;
  }
  return res;
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
  // destructor explicitly called if not submitted (submit() is not called)
  template <typename... Args>
  static Self &create(ngx_http_request_t &req, Context &ctx, dd::Span &span,
                      Args &&...extra_args) {
    ngx_thread_task_t *task = ngx_thread_task_alloc(req.pool, sizeof(Self));
    if (!task) {
      throw std::runtime_error{"failed to allocate task"};
    }
    Stats::task_created();

    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    auto *task_ctx =
        new (task->ctx) Self{req, ctx, span, std::forward<Args>(extra_args)...};
    task->handler = &PolTaskCtx::handler;
    task->event.handler = &PolTaskCtx::completion_handler;
    task->event.data = task_ctx;

    return *task_ctx;
  }

  ~PolTaskCtx() { Stats::task_destructed(); }

  // Takes an rvalue reference because once submitted the caller should no
  // longer interact with the task.
  bool submit(ngx_thread_pool_t *pool) &&noexcept {
    as_self().replace_handlers();

    req_.main->count++;

    bool simulate_task_post_failure = false;
    auto *main_conf = static_cast<datadog_main_conf_t *>(
        ngx_http_get_module_main_conf(&req_, ngx_http_datadog_module));
    if (main_conf &&
        main_conf->appsec_test_task_post_failure_mask != NGX_CONF_UNSET &&
        main_conf->appsec_test_task_post_failure_mask &
            Self::get_task_failure_flag()) {
      simulate_task_post_failure = true;
    }

    if (simulate_task_post_failure ||
        ngx_thread_task_post(pool, &get_task()) != NGX_OK) {
      ngx_log_error(NGX_LOG_ERR, req_.connection->log, 0,
                    "failed to post task %p", &get_task());

      Stats::task_submission_failed();

      req_.main->count--;
      as_self().restore_handlers();

      as_self().~Self();
      return false;
    }

    Stats::task_submitted();

    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, req_.connection->log, 0,
                   "task %p submitted. Request refcount: %d", &get_task(),
                   req_.main->count);

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

  ngx_log_t *req_log() const noexcept { return req_.connection->log; }

  static void handler(void *self, ngx_log_t *tp_log) noexcept {
    static_cast<Self *>(self)->handle(tp_log);
  }

  // runs on the thread pool
  void handle(ngx_log_t *tp_log) noexcept {
    try {
      ngx_log_debug(NGX_LOG_DEBUG_HTTP, req_log(), 0, "before task %p main",
                    this);
      block_spec_ = as_self().do_handle(*tp_log);
      // test long libddwaf call
      // ::usleep(2000000);
      ngx_log_debug(NGX_LOG_DEBUG_HTTP, req_log(), 0, "after task %p main",
                    this);
      ran_on_thread_.store(true, std::memory_order_release);
    } catch (std::exception &e) {
      ngx_log_error(NGX_LOG_ERR, tp_log, 0, "task %p failed: %s", this,
                    e.what());
    } catch (...) {
      ngx_log_error(NGX_LOG_ERR, tp_log, 0, "task %p failed: unknown failure",
                    this);
    }
  }

  // define in subclasses
  std::optional<BlockSpecification> do_handle(ngx_log_t &log) = delete;

  // runs on the main thread
  static void completion_handler(ngx_event_t *evt) noexcept {
    Stats::task_completed();
    auto *self = static_cast<Self *>(evt->data);
    self->completion_handler_impl();
  }

  void completion_handler_impl() noexcept {
    as_self().restore_handlers();

    auto count = req_.main->count;
    ngx_log_debug(NGX_LOG_DEBUG_HTTP, req_log(), 0,
                  "refcount before decrement upon task %p completion: %d", this,
                  count);

    // create a stack copy to safely call destructor later
    // the task is allocated on the request pool, so by the end of this method
    // maybe *this is no longer valid
    alignas(Self) char self_copy_storage[sizeof(Self)];
    std::memcpy(self_copy_storage, this, sizeof(Self));
    Self *self_copy = reinterpret_cast<Self *>(self_copy_storage);

    if (count > 1) {
      // ngx_del_event(connection->read, NGX_READ_EVENT, 0) may've been called
      // by ngx_http_block_reading
      if (ngx_handle_read_event(req_.connection->read, 0) != NGX_OK) {
        ngx_http_finalize_request(&req_, NGX_HTTP_INTERNAL_SERVER_ERROR);
        ngx_log_error(NGX_LOG_ERR, req_log(), 0,
                      "failed to re-enable read event after task %p", this);
      } else {
        req_.main->count--;
        ngx_log_debug1(NGX_LOG_DEBUG_HTTP, req_log(), 0,
                       "calling complete on task %p", this);
        as_self().complete();
        // req_ may be invalid at this point
      }
    } else {
      ngx_log_debug1(
          NGX_LOG_DEBUG_HTTP, req_log(), 0,
          "skipping run of completion handler for task %p because "
          "we're the only reference to the request; finalizing instead",
          this);
      ngx_http_finalize_request(&req_, NGX_DONE);
    }

    // call destructor on the safe stack copy instead of the original
    self_copy->~Self();
  }

  // define in subclasses
  void complete() noexcept = delete;

  void replace_handlers() noexcept {
    req_.read_event_handler = ngx_http_block_reading;
    req_.write_event_handler = PolTaskCtx<Self>::empty_write_handler;
  }

  void restore_handlers() noexcept {
    if (req_.read_event_handler == ngx_http_block_reading) {
      req_.read_event_handler = prev_read_evt_handler_;
    } else {
      ngx_log_error(NGX_LOG_ERR, req_log(), 0,
                    "unexpected read event handler %p; not restoring",
                    req_.read_event_handler);
    }
    if (req_.write_event_handler == PolTaskCtx<Self>::empty_write_handler) {
      req_.write_event_handler = prev_write_evt_handler_;
    } else {
      ngx_log_error(NGX_LOG_ERR, req_log(), 0,
                    "unexpected write event handler %p; not restoring",
                    req_.write_event_handler);
    }
  }

  Self &as_self() { return *static_cast<Self *>(this); }

  static void empty_write_handler(ngx_http_request_t *req) {
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, req->connection->log, 0,
                   "task wait empty handler");

    ngx_event_t *wev = req->connection->write;

    if (ngx_handle_write_event(wev, 0) != NGX_OK) {
      ngx_http_finalize_request(req, NGX_HTTP_INTERNAL_SERVER_ERROR);
    }
  }

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

  static ngx_int_t get_task_failure_flag() noexcept {
    return kTaskPostFailureMaskInitialWaf;
  }

  std::optional<BlockSpecification> do_handle(ngx_log_t &tp_log) {
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
      ngx_int_t rc;
      try {
        rc = service->block(*block_spec_, req_);

        ngx_log_debug(NGX_LOG_DEBUG_HTTP, req_.connection->log, 0,
                      "completion handler of waf start task: sent blocking "
                      "response (rc: %d, c: %d)",
                      rc, req_.main->count);
      } catch (const std::exception &e) {
        ngx_log_error(NGX_LOG_ERR, req_.connection->log, 0,
                      "failed to block request: %s", e.what());
        rc = NGX_ERROR;
      }

      ngx_log_debug(NGX_LOG_DEBUG_HTTP, req_.connection->log, 0,
                    "completion handler of waf start task: finish: calling "
                    "ngx_http_finalize_request with %d",
                    rc);
      ngx_http_finalize_request(&req_, rc);
      // the request may have been destroyed at this point
    } else {
      req_.phase_handler++;  // move past us
      ngx_post_event(req_.connection->write, &ngx_posted_events);
      ngx_log_debug0(NGX_LOG_DEBUG_HTTP, req_.connection->log, 0,
                     "completion handler of waf start task: normal finish");
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
  if (!waf_ctx_) {
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

  if (std::move(task_ctx).submit(conf->waf_pool)) {
    ngx_log_debug(NGX_LOG_DEBUG_HTTP, request.connection->log, 0,
                  "posted initial waf task");
    return true;
  }
  return false;
}

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

  dnsec::ClientIp ip_resolver{dnsec::Library::custom_ip_header(), req};
  auto client_ip = ip_resolver.resolve();

  ddwaf_object *data = collect_request_data(req, client_ip, memres_);

  client_ip_ = std::move(client_ip);

  auto &&log = *req.connection->log;
  auto [_, block_spec, tags_unused] = waf_ctx_->run(log, *data);

  if (block_spec) {
    stage_->store(stage::AFTER_BEGIN_WAF_BLOCK, std::memory_order_release);
  } else {
    stage_->store(stage::AFTER_BEGIN_WAF, std::memory_order_release);
  }

  return block_spec;
}

ngx_int_t Context::header_filter(ngx_http_request_t &request,
                                 dd::Span &span) noexcept {
  return catch_exceptions(
      "header_filter"sv, request,
      [&]() { return Context::do_header_filter(request, span); },
      static_cast<ngx_int_t>(NGX_ERROR));
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

  static ngx_int_t get_task_failure_flag() noexcept {
    return kTaskPostFailureMaskReqBodyWaf;
  }

  std::optional<BlockSpecification> do_handle(ngx_log_t &tp_log) {
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
      ngx_int_t rc;
      try {
        rc = service->block(*block_spec_, req_);
        ngx_log_debug(NGX_LOG_DEBUG_HTTP, req_.connection->log, 0,
                      "completion handler of waf req post task: sent "
                      "blocking response (rc: %d, c: %d)",
                      rc, req_.main->count);
      } catch (const std::exception &e) {
        ngx_log_error(NGX_LOG_ERR, req_.connection->log, 0,
                      "failed to block request: %s", e.what());
        rc = NGX_ERROR;
      }

      ngx_log_debug(NGX_LOG_DEBUG_HTTP, req_.connection->log, 0,
                    "completion handler of waf start task: finish: calling "
                    "ngx_http_finalize_request with %d",
                    rc);
      ngx_http_finalize_request(&req_, rc);
      // the request may have been destroyed at this point
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

  static ngx_int_t get_task_failure_flag() noexcept {
    return kTaskPostFailureMaskFinalWaf;
  }

  std::optional<BlockSpecification> do_handle(ngx_log_t &tp_log) {
    return ctx_.run_waf_end(req_, span_);
  }

  void complete() noexcept {
    bool const ran = ran_on_thread_.load(std::memory_order_acquire);
    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, req_.connection->log, 0,
                   "completion handler of waf req final task (ran: %s, "
                   "blocked: %s): start",
                   ran ? "true" : "false", block_spec_ ? "true" : "false");

    if (ran && block_spec_) {
      span_.set_tag("appsec.blocked"sv, "true"sv);
      ctx_.waf_final_done(req_, true);
      auto *service = BlockingService::get_instance();
      assert(service != nullptr);

      ngx_int_t rc;
      try {
        ngx_log_debug(NGX_LOG_DEBUG_HTTP, req_.connection->log, 0,
                      "completion handler of waf req final task: sending "
                      "blocking response");
        rc = service->block(*block_spec_, req_);
        ngx_log_debug(NGX_LOG_DEBUG_HTTP, req_.connection->log, 0,
                      "completion handler of waf req final task: sent blocking "
                      "response (rc: %d, c: %d)",
                      rc, req_.main->count);
      } catch (const std::exception &e) {
        ngx_log_error(NGX_LOG_ERR, req_.connection->log, 0,
                      "failed to block request: %s", e.what());
        rc = NGX_ERROR;
      }

      ngx_log_debug(NGX_LOG_DEBUG_HTTP, req_.connection->log, 0,
                    "completion handler of waf req final task: calling "
                    "ngx_http_finalize_request with %d",
                    rc);

      const auto count_before = req_.count;
      ngx_http_finalize_request(&req_, rc);
      // if count_before == 1, the request has likely been destroyed at this
      // point, although we cannot be sure (e.g. there may be a post action)
      if (count_before > 1) {
        ngx_post_event(req_.connection->write, &ngx_posted_events);
      }
      // req_ may be invalid at this point
    } else {
      ctx_.waf_final_done(req_, false);
      ngx_post_event(req_.connection->write, &ngx_posted_events);
      ngx_log_debug(NGX_LOG_DEBUG_HTTP, req_.connection->log, 0,
                    "not blocking after final waf run; triggering write event "
                    "on connection");
      if (req_.upstream) {
        // it may be that the body filter is never called again, so we have
        // no chance to send the buffered data.
        ctx_.prepare_drain_buffered_header(req_);
      }
    }
  }

  ngx_event_handler_pt orig_conn_write_handler_{};
  void replace_handlers() noexcept {
    auto &handler = req_.connection->write->handler;
    orig_conn_write_handler_ = handler;

    handler = ngx_http_empty_handler;
  }

  void restore_handlers() noexcept {
    req_.connection->write->handler = orig_conn_write_handler_;
  }

  friend PolTaskCtx;

 public:
  bool submit(ngx_thread_pool_t *pool, bool from_header_filter) noexcept {
    // if submission fails, the destructor is called, so save these
    Context &ctx = ctx_;
    ngx_http_request_t &req = req_;

    // with header_only, generally the body filter is not called and the
    // request is synchronously finalized after the header filter is called.
    // We need to be called from the body filter because that's where we
    // flush the buffered header.
    // (If the submission fails, this is not strictly required, because we could
    // flush the buffered header synchronously, but do it all the same to avoid
    // multiplying code paths)
    if (from_header_filter && req.header_only) {
      ngx_log_debug(NGX_LOG_DEBUG_HTTP, req.connection->log, 0,
                    "suppressing req.header_only");
      req.header_only = 0;
    }
    req.header_sent = 1;  // skip/alert when attempting to send headers

    // call super implementation
    bool submitted =
        static_cast<PolTaskCtx<PolFinalWafCtx> &&>(*this).submit(pool);
    if (!submitted) {
      ngx_log_debug(NGX_LOG_DEBUG_HTTP, req.connection->log, 0,
                    "submission of final waf task failed; moving directly to "
                    "stage AFTER_RUN_WAF_END and triggering write event on "
                    "connection");
      ctx.waf_final_done(req, false);
      ngx_post_event(req.connection->write, &ngx_posted_events);
      if (req.upstream) {
        // it may be that the body filter is never called again, so we have
        // no chance to send the buffered data.
        ctx.prepare_drain_buffered_header(req);
      }
    }

    return submitted;
  }
};

ngx_int_t Context::do_request_body_filter(ngx_http_request_t &request,
                                          ngx_chain_t *in, dd::Span &span) {
  auto st = stage_->load(std::memory_order_acquire);
  ngx_log_debug4(NGX_LOG_DEBUG_HTTP, request.connection->log, 0,
                 "waf request body filter %s in chain. accumulated=%uz, "
                 "copied=%uz, Stage: %d",
                 in ? "with" : "without", filter_ctx_.out_total,
                 filter_ctx_.copied_total, st);

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

    if (buffer_chain(filter_ctx_, *request.pool, in, true) != NGX_OK) {
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
      if (buffer_chain(filter_ctx_, *request.pool, in, false) != NGX_OK) {
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

      if (std::move(task_ctx).submit(conf->waf_pool)) {
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
      if (buffer_chain(filter_ctx_, *request.pool, in, true) != NGX_OK) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
      }
    }
  } else if (st == stage::AFTER_ON_REQ_WAF ||
             st == stage::AFTER_ON_REQ_WAF_BLOCK) {
    if (filter_ctx_.out) {  // first call after WAF ended
      ngx_log_debug1(NGX_LOG_DEBUG_HTTP, request.connection->log, 0,
                     "first filter call after WAF ended, req refcount=%d",
                     request.main->count);
      if (buffer_chain(filter_ctx_, *request.pool, in, false) != NGX_OK) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
      }

    pass_downstream:
      // pass saved buffers downstream
      auto rc = ngx_http_next_request_body_filter(&request, filter_ctx_.out);

      filter_ctx_.clear(*request.pool);

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
      if (buffer_chain(filter_ctx_, *request.pool, in, false) != NGX_OK) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
      }
    }
  } else {
    if (st != stage::DISABLED            /* no WAF for this request */
        && st != stage::ENTERED_ON_START /* could not submit 1st WAF task */
        && st != stage::AFTER_BEGIN_WAF_BLOCK /* blocked by 1st WAF task */) {
      ngx_log_error(NGX_LOG_NOTICE, request.connection->log, 0,
                    "unexpected filter call in stage %V", to_ngx_str(st));
    }
    return ngx_http_next_request_body_filter(&request, in);
  }

  // we continue to call the next filter, but don't pass any data
  return ngx_http_next_request_body_filter(&request, nullptr);
}

ngx_int_t Context::buffer_chain(FilterCtx &filter_ctx, ngx_pool_t &pool,
                                ngx_chain_t const *in, bool consume) noexcept {
  ngx_log_debug(
      NGX_LOG_DEBUG_HTTP, pool.log, 0,
      "buffer_chain: in=%p, chain_len=%uz, chain_size=%uz, consume=%d", in,
      chain::length(in), chain::size(in), consume);
  if (pool.log->log_level >= NGX_LOG_DEBUG) {
    for (auto *in_ch = in; in_ch; in_ch = in_ch->next) {
      const auto &buf = *in_ch->buf;
      ngx_log_error(
          NGX_LOG_DEBUG, pool.log, 0,
          "buffer_chain link: "
          "t:%d m: %d mmap: %d, r:%d f:%d fl:%d lb=%d s:%d %p %p-%p %p %O-%O",
          buf.temporary, buf.memory, buf.mmap, buf.recycled, buf.in_file,
          buf.flush, buf.last_buf, buf.sync, buf.start, buf.pos, buf.last,
          buf.file, buf.file_pos, buf.file_last);
    }
  }

  if (in && filter_ctx.found_last) {
    ngx_log_error(
        NGX_LOG_NOTICE, pool.log, 0,
        "given buffer after having already received one with ->last_buf");
    return NGX_ERROR;
  }

  for (auto *in_ch = in; in_ch; in_ch = in_ch->next) {
    ngx_chain_t *new_ch = ngx_alloc_chain_link(&pool);  // uninitialized
    if (!new_ch) {
      return NGX_ERROR;
    }

    auto *buf = in_ch->buf;
    size_t size;
    if (consume) {  // copy the buffer and consume the original
      ngx_buf_t *new_buf;
      if (!buf->in_file) {
        size = buf->last - buf->pos;

        if (size > 0) {
          new_buf = ngx_create_temp_buf(&pool, size);
          if (!new_buf) {
            return NGX_ERROR;
          }
          new_buf->last = ngx_copy(new_buf->pos, buf->pos, size);
          buf->pos = buf->last;  // consume
          filter_ctx.copied_total += size;
        } else {
          // special buffer
          if (!ngx_buf_special(buf)) {
            ngx_log_error(
                NGX_LOG_NOTICE, pool.log, 0,
                "unexpected empty non-special buffer in buffer_chain");
          }
          new_buf = static_cast<ngx_buf_t *>(ngx_calloc_buf(&pool));
          if (!new_buf) {
            return NGX_ERROR;
          }
          new_buf->flush = buf->flush;
          new_buf->sync = buf->sync;
        }
      } else {
        // file buffers (or mixed memory/file buffers)
        new_buf = static_cast<decltype(new_buf)>(ngx_calloc_buf(&pool));
        if (!new_buf) {
          return NGX_ERROR;
        }
        new_buf->in_file = 1;
        new_buf->file = buf->file;
        new_buf->file_pos = buf->file_pos;
        new_buf->file_last = buf->file_last;

        buf->file_pos = buf->file_last;  // consume

        // mixed
        if (buf->temporary) {
          size = buf->last - buf->pos;
          new_buf->temporary = 1;
          if (size > 0) {
            new_buf->pos = static_cast<u_char *>(ngx_palloc(&pool, size));
            if (!new_buf->pos) {
              return NGX_ERROR;
            }
            new_buf->last = ngx_copy(new_buf->pos, buf->pos, size);
            buf->pos = buf->last;  // consume
            filter_ctx.copied_total += size;
          }
        }

        size = new_buf->file_last - new_buf->file_pos;
      }

      new_buf->last_buf = buf->last_buf;
      new_buf->tag = reinterpret_cast<void *>(kBufferTag);
      new_ch->buf = new_buf;
    } else {  // do not consume
      size = ngx_buf_size(buf);
      new_ch->buf = buf;
    }
    new_ch->next = nullptr;

    filter_ctx.out_total += size;
    if (buf->last_buf) {
      filter_ctx.found_last = true;
    }

    *filter_ctx.out_latest = new_ch;
    filter_ctx.out_latest = &new_ch->next;
  }

  return NGX_OK;
}

ngx_int_t Context::buffer_header_output(ngx_pool_t &pool,
                                        ngx_chain_t *chain) noexcept {
  ngx_log_debug(NGX_LOG_DEBUG_HTTP, pool.log, 0,
                "buffer_header_output: saved_(len,size)=(%uz,%uz), "
                "in_(len,size)=(%uz,%uz)",
                chain::length(header_filter_ctx_.out),
                chain::size(header_filter_ctx_.out), chain::length(chain),
                chain::size(chain));

  ngx_int_t res = buffer_chain(header_filter_ctx_, pool, chain, true);

  ngx_log_debug(NGX_LOG_DEBUG_HTTP, pool.log, 0,
                "buffer_header_output: buffer_chain output was %d, "
                "saved_(len,size)=(%uz,%uz)",
                res, chain::length(header_filter_ctx_.out),
                chain::size(header_filter_ctx_.out));

  return res;
}

ngx_int_t Context::send_buffered_header(ngx_http_request_t &request) noexcept {
  ngx_log_debug(NGX_LOG_DEBUG_HTTP, request.connection->log, 0,
                "send_buffered_header: buffered_(len,size)=(%uz,%uz)",
                chain::length(header_filter_ctx_.out),
                chain::size(header_filter_ctx_.out));

  if (!request.stream) {
    ngx_int_t rc = ngx_http_write_filter(&request, header_filter_ctx_.out);
    if (rc == NGX_ERROR) {
      request.connection->error = 1;
    }
    header_filter_ctx_.clear(*request.pool);
    ngx_log_debug(NGX_LOG_DEBUG_HTTP, request.connection->log, 0,
                  "send_buffered_header: ngx_http_write_filter returned %d",
                  rc);
    return rc;
  }

  // http/2
  ngx_connection_t &c = *request.stream->connection->connection;
  ngx_chain_t *rem_chain = c.send_chain(&c, header_filter_ctx_.out, 0);
  if (rem_chain == NGX_CHAIN_ERROR) {
    ngx_log_error(NGX_LOG_NOTICE, c.log, 0,
                  "send_buffered_header: send_chain failed");
    return NGX_ERROR;
  }

  ngx_log_debug(NGX_LOG_DEBUG_HTTP, c.log, 0,
                "send_buffered_header: remaining chain_(len,size)=(%uz,%uz)",
                chain::length(rem_chain), chain::size(rem_chain));
  for (ngx_chain_t *cl = header_filter_ctx_.out; cl && cl != rem_chain;) {
    ngx_chain_t *ln = cl;
    cl = cl->next;
    ngx_free_chain(c.pool, ln);
  }
  header_filter_ctx_.replace_out(rem_chain);
  if (rem_chain == nullptr) {
    return NGX_OK;
  }
  return NGX_AGAIN;
}

namespace {
Context *get_sec_ctx(ngx_http_request_t *random_data) noexcept {
  auto *dd_ctx = static_cast<DatadogContext *>(
      ngx_http_get_module_ctx(random_data, ngx_http_datadog_module));
  if (dd_ctx) {
    return dd_ctx->get_security_context();
  }
  return static_cast<Context *>(nullptr);
}
}  // namespace

void Context::drain_buffered_data_write_handler(
    ngx_http_request_t *r) noexcept {
  ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                 "drain_buffered_data_write_handler called");

  Context *ctx = get_sec_ctx(r);
  if (!ctx) {
    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                  "drain_buffered_data_write_handler: no context");
    ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
    return;
  }

  ngx_connection_t *c = r->connection;  // fake connection on http2
  ngx_event_t *wev = c->write;
  ngx_http_core_loc_conf_t *clcf = static_cast<decltype(clcf)>(
      ngx_http_get_module_loc_conf(r->main, ngx_http_core_module));

  if (wev->timedout) {
    ngx_log_error(NGX_LOG_INFO, c->log, NGX_ETIMEDOUT,
                  "drain_buffered_data_write_handler: client timed out");
    c->timedout = 1;
    c->error = 1;

    ngx_http_finalize_request(r, NGX_HTTP_REQUEST_TIME_OUT);
    return;
  }

  if (wev->delayed || r->aio) {
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, wev->log, 0,
                   "drain_buffered_data_write_handler: http writer delayed");

    if (!wev->delayed) {
      ngx_add_timer(wev, clcf->send_timeout);
    }

    if (ngx_handle_write_event(wev, clcf->send_lowat) != NGX_OK) {
      ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
    }

    return;
  }

  ngx_log_debug(NGX_LOG_DEBUG_HTTP, c->log, 0,
                "drain_buffered_data_write_handler: about to call "
                "ngx_http_output_filter");
  ngx_int_t rc = ngx_http_output_filter(r, NULL);

  ngx_log_debug3(NGX_LOG_DEBUG_HTTP, c->log, 0,
                 "drain_buffered_data_write_handler: http writer output "
                 "filter: %i, \"%V?%V\"",
                 rc, &r->uri, &r->args);

  if (rc == NGX_ERROR) {
    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,
                  "drain_buffered_header_write_handler: send_buffered_header "
                  "failed");
    ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
    return;
  }

  if (rc == NGX_AGAIN || r->buffered || r->postponed ||
      (r == r->main && c->buffered)) {
    ngx_log_debug(
        NGX_LOG_DEBUG_HTTP, c->log, 0,
        "drain_buffered_data_write_handler: http writer still has data to "
        "write");

    if (!wev->delayed) {
      ngx_add_timer(wev, clcf->send_timeout);
    }

    if (ngx_handle_write_event(wev, clcf->send_lowat) != NGX_OK) {
      ngx_http_finalize_request(r, NGX_HTTP_INTERNAL_SERVER_ERROR);
    }

    return;
  }

  ngx_log_debug0(NGX_LOG_DEBUG_HTTP, r->connection->log, 0,
                 "drain_buffered_header_write_handler: send_buffered_header "
                 "succeeded; restoring handler and triggering write");
  r->write_event_handler = ctx->prev_req_write_evt_handler_;
  ngx_post_event(r->connection->write, &ngx_posted_events);
}

void Context::prepare_drain_buffered_header(
    ngx_http_request_t &request) noexcept {
  if (!header_filter_ctx_.out) {
    ngx_log_debug(NGX_LOG_DEBUG_HTTP, request.connection->log, 0,
                  "prepare_drain_buffered_header: no buffered header to drain");
    return;
  }

  ngx_log_debug(NGX_LOG_DEBUG_HTTP, request.connection->log, 0,
                "prepare_drain_buffered_header: buffered_(len,size)=(%uz,%uz)",
                chain::length(header_filter_ctx_.out),
                chain::size(header_filter_ctx_.out));

  prev_req_write_evt_handler_ = request.write_event_handler;
  request.write_event_handler = drain_buffered_data_write_handler;

  if (!header_filter_ctx_.found_last) {
    ngx_log_debug(
        NGX_LOG_DEBUG_HTTP, request.connection->log, 0,
        "prepare_drain_buffered_header: adding a flush to last chain link");

    for (auto *cl = header_filter_ctx_.out; cl; cl = cl->next) {
      if (cl->next == nullptr) {
        cl->buf->flush = 1;
        break;
      }
    }
  }
}

void Context::FilterCtx::clear(ngx_pool_t &pool) noexcept {
  for (ngx_chain_t *cl = out; cl;) {
    ngx_chain_t *ln = cl;
    cl = cl->next;
    ngx_free_chain(&pool, ln);
  }

  out = nullptr;
  out_latest = &out;
  out_total = 0;
  copied_total = 0;
  // found_last retained
}

void Context::FilterCtx::replace_out(ngx_chain_t *new_out) noexcept {
  out = new_out;
  copied_total = 0;
  out_total = 0;
  ngx_chain_t **lastp = &out;
  for (; *lastp; lastp = &(*lastp)->next) {
    out_total += ngx_buf_size((*lastp)->buf);
  }
  out_latest = lastp;
}

namespace {
class Http1TemporarySendChain {
 public:
  static Http1TemporarySendChain instance;
  void activate(Context &ctx, ngx_http_request_t &request) noexcept {
    current_ctx_ = &ctx;
    current_pool_ = request.pool;
    prev_send_chain_ = request.connection->send_chain;
    request.connection->send_chain = send_chain_save;
  }

  void deactivate(ngx_http_request_t &request) noexcept {
    auto *ctx = instance.current_ctx_;
    assert(ctx != nullptr);

    if (request.out) {
      // there is uncommitted data in the output chain; send_chain() was not
      // called; ngx_http_write_filter() chose not to do it
      ngx_log_debug(NGX_LOG_DEBUG_HTTP, request.connection->log, 0,
                    "temporary send_chain: uncommitted data in output chain "
                    "(%uz bytes); adding it to the header output buffer",
                    chain::size(request.out));
      ctx->buffer_header_output(*request.pool, request.out);
      for (auto *cl = request.out; cl;) {
        auto *next = cl->next;
        ngx_free_chain(request.pool, cl);
        cl = next;
      }
      request.out = nullptr;
      request.connection->buffered &= ~NGX_HTTP_WRITE_BUFFERED;
    }

    current_ctx_ = nullptr;
    request.connection->send_chain = prev_send_chain_;
  }

 private:
  static ngx_chain_t *send_chain_save([[maybe_unused]] ngx_connection_t *c,
                                      ngx_chain_t *in,
                                      [[maybe_unused]] off_t limit) {
    auto *ctx = instance.current_ctx_;
    assert(ctx != nullptr);
    auto *pool = instance.current_pool_;
    if (ctx->buffer_header_output(*pool, in) != NGX_OK) {
      return NGX_CHAIN_ERROR;
    }

    return nullptr;
  }

  Context *current_ctx_;
  ngx_pool_t *current_pool_;
  ngx_send_chain_pt prev_send_chain_;
};
Http1TemporarySendChain Http1TemporarySendChain::instance;

class Http2TemporarySendChain {
 public:
  static Http2TemporarySendChain instance;
  void activate(Context &ctx, ngx_http_request_t &request) noexcept {
    current_ctx_ = &ctx;
    pool_ = request.pool;

    ngx_http_v2_connection_t &h2c = *request.stream->connection;
    // test forceful NGX_AGAIN: h2c.connection->write->ready = 0;
    ngx_send_chain_pt &stream_sc = h2c.connection->send_chain;
    prev_send_chain_ = stream_sc;

    // ngx_http_v2_header_filter calls ngx_http_v2_queue_blocked_frame
    // This puts the header frame either at the end of the chain (the first
    // frame to go on the wire) or just before the first blocked frame (in
    // wire order, just after the last blocked frame).
    ngx_http_v2_out_frame_t **frame_ip;  // insertion point
    for (frame_ip = &h2c.last_out; *frame_ip; frame_ip = &(*frame_ip)->next) {
      if ((*frame_ip)->blocked || (*frame_ip)->stream == nullptr) {
        break;
      }
    }
    frame_ip_ = frame_ip;
    frame_ip_value_ = *frame_ip;
    stream_sc = stream_send_chain_save;
  }

  void deactivate(ngx_http_request_t &request) noexcept {
    auto *ctx = instance.current_ctx_;
    assert(ctx != nullptr);

    // we either flushed all the frames (because our replacement send_chain
    // consumes all the buffers), or we flushed nothing
    // (ngx_http_v2_send_output_queue returns NGX_AGAIN or error before
    // calling send_chain). Consequently, if last_out is set, then frame_ip_
    // points to a valid frame (i.e., we're in the case where nothing was
    // flushed)
    if (*frame_ip_ != frame_ip_value_) {
      // in this case, *frame_ip has to point to the header frame
      assert(*frame_ip_ != nullptr);
      ngx_http_v2_out_frame_t &header_frame = **frame_ip_;
      assert(header_frame.first->buf->pos[3] == NGX_HTTP_V2_HEADERS_FRAME);
      ngx_log_debug(NGX_LOG_DEBUG_HTTP, request.connection->log, 0,
                    "temporary send_chain: found header frame in uncommitted "
                    "h2c connection data frame_(len,size)=(%uz,%uz bytes); "
                    "adding it to the header output buffer and unqueuing it",
                    chain::length(header_frame.first),
                    chain::size(header_frame.first));
      ctx->buffer_header_output(*request.pool, header_frame.first);
      ngx_http_v2_connection_t *h2c = request.stream->connection;
      // calls ngx_http_v2_data_frame_handler:
      header_frame.handler(h2c, &header_frame);
      *frame_ip_ = header_frame.next;
      // test forceful NGX_AGAIN: h2c.connection->write->ready = 1;
    }

    current_ctx_ = nullptr;
    pool_ = nullptr;
    ngx_http_v2_connection_t &h2c = *request.stream->connection;
    h2c.connection->send_chain = prev_send_chain_;
    frame_ip_ = nullptr;
    frame_ip_value_ = nullptr;
  }

 private:
  static ngx_chain_t *stream_send_chain_save(ngx_connection_t *c,
                                             ngx_chain_t *in,
                                             [[maybe_unused]] off_t limit) {
    ngx_http_request_t *req = static_cast<decltype(req)>(c->data);
    assert(req != nullptr);

    Context *ctx = instance.current_ctx_;
    assert(ctx != nullptr);
    if (ctx->buffer_header_output(*instance.pool_, in) != NGX_OK) {
      return NGX_CHAIN_ERROR;
    }

    return nullptr;
  }

  Context *current_ctx_;
  ngx_pool_t *pool_;
  ngx_send_chain_pt prev_send_chain_;
  ngx_http_v2_out_frame_t **frame_ip_;
  ngx_http_v2_out_frame_t *frame_ip_value_;
};
Http2TemporarySendChain Http2TemporarySendChain::instance;
}  // namespace

ngx_int_t Context::do_header_filter(ngx_http_request_t &request,
                                    dd::Span &span) {
  auto st = stage_->load(std::memory_order_acquire);
  ngx_log_debug4(NGX_LOG_DEBUG_HTTP, request.connection->log, 0,
                 "waf header filter in stage %V, header_sent=%d, "
                 "buf_header_data_(len,size)=(%uz,%uz)",
                 to_ngx_str(st), request.header_sent,
                 chain::length(header_filter_ctx_.out),
                 chain::size(header_filter_ctx_.out));

  if (st != stage::AFTER_BEGIN_WAF && st != stage::AFTER_ON_REQ_WAF) {
    return ngx_http_next_header_filter(&request);
  }

  ngx_log_debug(NGX_LOG_DEBUG_HTTP, request.connection->log, 0,
                "waf header filter: replacing send_chain handler "
                "and invoking the next header filter");

  if (request.header_only) {
    // save it; we'll change req.header_only upon waf task submission
    header_only_ = true;
  }

  // first, buffer the header data
  ngx_int_t rc;
  if (request.stream) {
    // http/2
    Http2TemporarySendChain::instance.activate(*this, request);
    rc = ngx_http_next_header_filter(&request);
    Http2TemporarySendChain::instance.deactivate(request);
  } else {
    // http/1.x or http/3
    Http1TemporarySendChain::instance.activate(*this, request);
    rc = ngx_http_next_header_filter(&request);
    Http1TemporarySendChain::instance.deactivate(request);
  }
  if (rc != NGX_OK) {
    ngx_log_error(NGX_LOG_ERR, request.connection->log, 0,
                  "waf header filter: downstream filters returned %d", rc);
    transition_to_stage(stage::AFTER_RUN_WAF_END);
    return rc;
  }

  // If no body (or we're ignoring it), start the WAF
  // Afterwards, we transition to PENDING_WAF_END (or AFTER_RUN_WAF_END if we
  // were unable to submit the WAF task)
  waf_send_resp_body_ = waf_send_resp_body_ && is_body_resp_parseable(request);
  if (!waf_send_resp_body_) {
    ngx_log_debug(NGX_LOG_DEBUG_HTTP, request.connection->log, 0,
                  "waf header filter: downstream filters returned NGX_OK; "
                  "attempting to submit WAF task (waf_send_resp_body=false, "
                  "header_only=%d, content_length=%d)",
                  request.header_only, request.headers_out.content_length_n);

    PolFinalWafCtx &task_ctx = PolFinalWafCtx::create(request, *this, span);
    auto *conf = static_cast<datadog_loc_conf_t *>(
        ngx_http_get_module_loc_conf(&request, ngx_http_datadog_module));

    transition_to_stage(stage::PENDING_WAF_END);

    if (task_ctx.submit(conf->waf_pool, true /* from_header_filter */)) {
      return NGX_OK;
    } else {
      ngx_log_error(NGX_LOG_NOTICE, request.connection->log, 0,
                    "waf header filter: failed to post waf end task");

      return NGX_OK;
    }
  }

  ngx_log_debug(
      NGX_LOG_DEBUG_HTTP, request.connection->log, 0,
      "waf header filter: waiting for enough body data to do final WAF run");
  transition_to_stage(stage::COLLECTING_ON_RESP_DATA);
  return NGX_OK;
}

ngx_int_t Context::do_output_body_filter(ngx_http_request_t &request,
                                         ngx_chain_t *const in,
                                         dd::Span &span) {
  auto st = stage_->load(std::memory_order_acquire);
  ngx_log_debug(
      NGX_LOG_DEBUG_HTTP, request.connection->log, 0,
      "waf output body filter: in stage %V, header_sent=%d, "
      "header_(len_sized)=(%uz,%uz), out_(len,size,copied)=(%uz,%uz,%uz), "
      "in_chain_(len,size)=(%uz,%uz), l:%d, s:%d",
      to_ngx_str(st), request.header_sent,
      chain::length(header_filter_ctx_.out),
      chain::size(header_filter_ctx_.out), chain::length(out_filter_ctx_.out),
      chain::size(out_filter_ctx_.out), out_filter_ctx_.copied_total,
      chain::length(in), chain::size(in), chain::has_last(in),
      chain::has_special(in));

  if (header_only_ && !request.header_only) {
    ngx_log_debug(NGX_LOG_DEBUG_HTTP, request.connection->log, 0,
                  "waf output body filter: restoring to 1 the value of "
                  "request.header_only");
    request.header_only = 1;
  }

  const bool buffering =
      st == stage::PENDING_WAF_END || st == stage::COLLECTING_ON_RESP_DATA;
  if (buffering) {
    request.buffered |= 0x08;

    bool consume;
    if (out_filter_ctx_.copied_total >= max_saved_output_data_) {
      ngx_log_debug(NGX_LOG_DEBUG_HTTP, request.connection->log, 0,
                    "waf output body filter: too much copied data (%uz bytes "
                    ">= %uz), avoiding consuming more",
                    out_filter_ctx_.copied_total, max_saved_output_data_);
      consume = false;
    } else {
      consume = true;
    }

    if (buffer_chain(out_filter_ctx_, *request.pool, in, consume) != NGX_OK) {
      return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    ngx_log_debug2(NGX_LOG_DEBUG_HTTP, request.connection->log, 0,
                   "waf output body filter: there are now %uz bytes of "
                   "accumulated output data (%uz copied)",
                   out_filter_ctx_.out_total, out_filter_ctx_.copied_total);

    const bool start_waf =
        st == stage::COLLECTING_ON_RESP_DATA &&
        (out_filter_ctx_.out_total >= kMaxFilterData || chain::has_last(in));

    if (start_waf) {
      if (out_filter_ctx_.out_total == 0) {
        ngx_log_debug(NGX_LOG_DEBUG_HTTP, request.connection->log, 0,
                      "waf output body filter: no data accumulated; "
                      "running WAF end without body");
        waf_send_resp_body_ = false;
      } else {
        ngx_log_debug(NGX_LOG_DEBUG_HTTP, request.connection->log, 0,
                      "waf output body filter: enough data to do final WAF run "
                      "(out total is %uz, has_last=%d)",
                      out_filter_ctx_.out_total, chain::has_last(in));
      }

      PolFinalWafCtx &task_ctx = PolFinalWafCtx::create(request, *this, span);
      auto *conf = static_cast<datadog_loc_conf_t *>(
          ngx_http_get_module_loc_conf(&request, ngx_http_datadog_module));
      transition_to_stage(stage::PENDING_WAF_END);
      if (task_ctx.submit(conf->waf_pool, false /* from_header_filter */)) {
        ngx_log_debug(NGX_LOG_DEBUG_HTTP, request.connection->log, 0,
                      "waf output body filter: submitted waf end task; "
                      "transitioning to PENDING_WAF_END");
      } else {
        ngx_log_error(NGX_LOG_NOTICE, request.connection->log, 0,
                      "waf output body filter: failed to post waf end task");
      }
    }

    return consume ? NGX_OK : NGX_AGAIN;
  }  // if (buffering)

  // !buffering
  request.buffered &= ~0x08;

  if (st == stage::WAF_END_BLOCK_COMMIT) {
    // commit of body of blocking response
    assert(request.header_sent == 1);
    ngx_log_debug1(NGX_LOG_DEBUG_HTTP, request.connection->log, 0,
                   "waf output body filter: discarding %uz bytes of "
                   "accumulated data and passing down blocking response data",
                   out_filter_ctx_.out_total);
    header_filter_ctx_.clear(*request.pool);
    out_filter_ctx_.clear(*request.pool);

    transition_to_stage(stage::AFTER_RUN_WAF_END);
    return ngx_http_next_output_body_filter(&request, in);
  }

  // otherwise we send down the buffered data + whatever we got

  if (header_filter_ctx_.out) {
    // if we have header data, send it first
    ngx_log_debug(NGX_LOG_DEBUG_HTTP, request.connection->log, 0,
                  "waf output body filter: sending down buffered header data");
    ngx_int_t rc = send_buffered_header(request);
    if (rc == NGX_AGAIN) {
      ngx_log_debug(NGX_LOG_DEBUG_HTTP, request.connection->log, 0,
                    "waf output body filter: send_buffered_header returned "
                    "NGX_AGAIN after sending down buffered header data; "
                    "returning NGX_AGAIN");
      request.buffered |= 0x08;
      return NGX_AGAIN;
    }
    if (rc != NGX_OK) {
      ngx_log_debug(NGX_LOG_DEBUG_HTTP, request.connection->log, 0,
                    "waf output body filter: ngx_http_write_filter returned %d "
                    "after sending down buffered header data; returning",
                    rc);
      return rc;
    }
    // else continue
  }

  if (out_filter_ctx_.out) {
    // if we have data accumulated, add in to it (without consuming) and send it
    // downstream. We add it all together to avoid having to do two calls
    // downstream (plus handle the case where the first call doesn't return
    // NGX_OK)
    if (buffer_chain(out_filter_ctx_, *request.pool, in, false) != NGX_OK) {
      return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    ngx_log_debug(NGX_LOG_DEBUG_HTTP, request.connection->log, 0,
                  "waf output body filter: sending down buffered out + in "
                  "chain, accum=%uz, copied=%uz, lb=%d",
                  out_filter_ctx_.out_total, out_filter_ctx_.copied_total,
                  out_filter_ctx_.found_last);

    auto rc = ngx_http_next_output_body_filter(&request, out_filter_ctx_.out);
    ngx_log_debug(NGX_LOG_DEBUG_HTTP, request.connection->log, 0,
                  "waf output body filter: downstream filter returned %d", rc);
    out_filter_ctx_.clear(*request.pool);
    return rc;
  } else {
    // just pass in chain through
    ngx_log_debug(NGX_LOG_DEBUG_HTTP, request.connection->log, 0,
                  "waf output body filter: sending down in chain directly");
    auto rc = ngx_http_next_output_body_filter(&request, in);
    ngx_log_debug(NGX_LOG_DEBUG_HTTP, request.connection->log, 0,
                  "waf_output_body_filter: downstream filter returned %d", rc);
    return rc;
  }
}

std::optional<BlockSpecification> Context::run_waf_req_post(
    ngx_http_request_t &request, dd::Span &span) {
  ddwaf_obj input;
  ddwaf_map_obj &input_map = input.make_map(1, memres_);
  ddwaf_obj &entry = input_map.at_unchecked(0);
  entry.set_key("server.request.body"sv);

  bool success = parse_body_req(entry, request, *filter_ctx_.out,
                                filter_ctx_.out_total, memres_);

  if (!success) {
    ngx_log_debug0(NGX_LOG_DEBUG_HTTP, request.connection->log, 0,
                   "failed to parse request body for WAF");
    return std::nullopt;
  }

  auto &&log = *request.connection->log;
  auto [_, block_spec, tags_unused] = waf_ctx_->run(log, input);
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

void Context::waf_final_done(ngx_http_request_t &request, bool blocked) {
  bool res = checked_transition_to_stage(
      stage::PENDING_WAF_END,
      blocked ? stage::WAF_END_BLOCK_COMMIT : stage::AFTER_RUN_WAF_END);

  if (!res) {
    ngx_log_error(NGX_LOG_ERR, request.connection->log, 0,
                  "call to waf_final_done without current stage being "
                  "PENDING_WAF_END");
    return;
  }
}

std::optional<BlockSpecification> Context::run_waf_end(
    ngx_http_request_t &request, dd::Span &span) {
  auto st = stage_->load(std::memory_order_acquire);
  if (st != stage::PENDING_WAF_END) {
    return std::nullopt;
  }

  ngx_chain_t *body_chain;
  size_t body_size;
  if (waf_send_resp_body_) {
    body_chain = out_filter_ctx_.out;
    body_size = std::min(out_filter_ctx_.out_total, kMaxFilterData);
  } else {
    body_chain = nullptr;
    body_size = 0;
  }

  bool extract_schema = Library::api_security_should_sample();
  ddwaf_object *resp_data = collect_response_data(
      request, body_chain, body_size, extract_schema, memres_);

  auto &&log = *request.connection->log;
  auto [_, block_spec, tags] = waf_ctx_->run(log, *resp_data);

  for (auto &[key, json] : tags) {
    ngx_log_debug(NGX_LOG_DEBUG_HTTP, &log, 0, "Setting tag %s with value %s",
                  key.c_str(), json.c_str());
    span.set_tag(key, json);
  }

  return block_spec;
}

bool Context::has_matches() const noexcept {
  return waf_ctx_ && waf_ctx_->has_matches();
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

  set_header_tags(waf_ctx_->has_matches(), request, span);
  report_matches(request, span);
  report_client_ip(span);
}

void Context::report_matches(ngx_http_request_t &request, dd::Span &span) {
  auto &&trace_segment = span.trace_segment();

  bool did_report = waf_ctx_->report_matches([&](std::string_view json) {
    ngx_str_t json_ns{dnsec::ngx_stringv(json)};

    if (request.connection->log->log_level & NGX_LOG_DEBUG_HTTP) {
      ngx_log_debug(NGX_LOG_DEBUG_HTTP, request.connection->log, 0,
                    "reporting an appsec event: %V", &json_ns);
    } else {
      ngx_log_error(NGX_LOG_INFO, request.connection->log, 0,
                    "reporting an appsec event", &json_ns);
    }

    span.set_tag("_dd.appsec.json"sv, json);
  });

  if (did_report) {
    trace_segment.override_sampling_priority(2);  // USER-KEEP
    span.set_tag("appsec.event"sv, "true");
    if (!apm_tracing_enabled_) {
      span.set_source(tracing::Source::appsec);
    }
  }
}

void Context::report_client_ip(dd::Span &span) const {
  if (!client_ip_) {
    return;
  }

  span.set_tag("http.client_ip"sv, *client_ip_);
}

}  // namespace datadog::nginx::security
