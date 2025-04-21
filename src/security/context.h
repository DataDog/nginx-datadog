#pragma once

#include <datadog/span.h>
#include <ddwaf.h>
#include <sys/types.h>

#include <atomic>
#include <cstddef>
#include <memory>
#include <optional>

#include "../dd.h"
#include "blocking.h"
#include "library.h"
#include "util.h"

extern "C" {
#include <nginx.h>
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <ngx_thread_pool.h>
}

namespace datadog::nginx::security {

// the tag used for the buffers we allocate
static constexpr uintptr_t kBufferTag = 0xD47AD06;

struct DdwafResultFreeFunctor {
  void operator()(ddwaf_result &res) { ddwaf_result_free(&res); }
};
struct OwnedDdwafResult
    : FreeableResource<ddwaf_result, DdwafResultFreeFunctor> {
  using FreeableResource::FreeableResource;
};

struct DdwafContextFreeFunctor {
  void operator()(ddwaf_context ctx) { ddwaf_context_destroy(ctx); }
};
struct OwnedDdwafContext
    : FreeableResource<ddwaf_context, DdwafContextFreeFunctor> {
  using FreeableResource::FreeableResource;
  explicit OwnedDdwafContext(std::nullptr_t) : FreeableResource{nullptr} {}
  OwnedDdwafContext &operator=(ddwaf_context ctx) {
    if (resource) {
      ddwaf_context_destroy(resource);
    }
    resource = ctx;
    return *this;
  }
};

class Context {
  Context(std::shared_ptr<OwnedDdwafHandle> waf_handle);

 public:
  // returns a new context or an empty unique_ptr if the waf is not active
  static std::unique_ptr<Context> maybe_create(
      datadog_loc_conf_t &loc_conf,
      std::optional<std::size_t> max_saved_output_data);

  ngx_int_t request_body_filter(ngx_http_request_t &request, ngx_chain_t *chain,
                                dd::Span &span) noexcept;

  bool on_request_start(ngx_http_request_t &request, dd::Span &span) noexcept;

  ngx_int_t header_filter(ngx_http_request_t &request, dd::Span &span) noexcept;

  ngx_int_t output_body_filter(ngx_http_request_t &request, ngx_chain_t *chain,
                               dd::Span &span) noexcept;

  void on_main_log_request(ngx_http_request_t &request,
                           dd::Span &span) noexcept;

  // runs on a separate thread; returns whether it blocked
  std::optional<BlockSpecification> run_waf_start(ngx_http_request_t &request,
                                                  dd::Span &span);

  std::optional<BlockSpecification> run_waf_req_post(
      ngx_http_request_t &request, dd::Span &span);

  void waf_req_post_done(ngx_http_request_t &request, bool blocked);

  void waf_final_done(ngx_http_request_t &request, bool blocked);

  std::optional<BlockSpecification> run_waf_end(ngx_http_request_t &request,
                                                dd::Span &span);

 private:
  bool do_on_request_start(ngx_http_request_t &request, dd::Span &span);
  ngx_int_t do_request_body_filter(ngx_http_request_t &request,
                                   ngx_chain_t *chain, dd::Span &span);
  ngx_int_t do_header_filter(ngx_http_request_t &request, dd::Span &span);
  ngx_int_t do_output_body_filter(ngx_http_request_t &request,
                                  ngx_chain_t *chain, dd::Span &span);
  void do_on_main_log_request(ngx_http_request_t &request, dd::Span &span);

  bool has_matches() const noexcept;
  void report_matches(ngx_http_request_t &request, dd::Span &span);
  void report_client_ip(dd::Span &span) const;

  enum class stage {
    /* Set on on_request_start (NGX_HTTP_ACCESS_PHASE) if there's no thread
     * pool mapped for the request.
     * Incoming transitions: START → DISABLED */
    DISABLED,

    /* Initial state, upon DatadogContext creation on on_enter
     * (NGX_HTTP_REWRITE_PHASE) */
    START,

    /* Set on on_request_start (NGX_HTTP_ACCESS_PHASE) in normal conditions.
     * The request may be suspended for 1st WAF run after entering this stage.
     * If submission fails, the stage will remain at this value (will not
     * transition to AFTER_BEGIN_WAF/AFTER_BEGIN_WAF_BLOCK).
     * Incoming transitions: START → ENTERED_ON_START. */
    ENTERED_ON_START,

    /* After the 1st WAF run, the state transitions to this stage if WAF did not
     * indicate a block. The write happens on the WAF thread with the request
     * suspended.
     * Incoming transitions: ENTERED_ON_START → AFTER_BEGIN_WAF */
    AFTER_BEGIN_WAF,

    /* Similar to AFTER_BEGIN_WAF, but when the WAF told us to block.
     * In this case we won't further run the WAF.
     * Incoming transitions: ENTERED_ON_START → AFTER_BEGIN_WAF_BLOCK */
    AFTER_BEGIN_WAF_BLOCK,

    /* Set on the request body filter when its first call is a preread
     * call. The filter transitions to COLLECTING_ON_REQ_DATA before the it
     * returns.
     * Incoming transitions: AFTER_BEGIN_WAF → COLLECTING_ON_REQ_DATA_PREREAD */
    COLLECTING_ON_REQ_DATA_PREREAD,

    /* The request body filter is collecting data to call the WAF.
     * Incoming transitions: AFTER_BEGIN_WAF → COLLECTING_ON_REQ_DATA_PREREAD
     *                       COLLECTING_ON_REQ_DATA_PREREAD →
     *                         COLLECTING_ON_REQ_DATA */
    COLLECTING_ON_REQ_DATA,

    /* The request body filter is suspended after the submiting the WAF task
     * on the request body.
     * Incoming transitions: COLLECTING_ON_REQ_DATA → SUSPENDED_ON_REQ_WAF */
    SUSPENDED_ON_REQ_WAF,

    /* Set on the complete() handler of the WAF task (main thread, if request
     * is live after the WAF completes) if we're not to block. Also set directly
     * in the request body filter if there is no data to run the WAF on or if
     * submission of the WAF task fails.
     * Incoming transitions: SUSPENDED_ON_REQ_WAF → AFTER_ON_REQ_WAF
     *                       COLLECTING_ON_REQ_DATA → AFTER_ON_REQ_WAF (no data)
     */
    AFTER_ON_REQ_WAF,

    /* Set on the complete() handler of the WAF if we're to block.
     * Incoming transitions: SUSPENDED_ON_REQ_WAF → AFTER_ON_REQ_WAF_BLOCK */
    AFTER_ON_REQ_WAF_BLOCK,

    /* Set on the 1st call of the output body filter, just before trying to
     * submit the WAF final run.
     * Incoming transitions: AFTER_BEGIN_WAF → BEFORE_RUN_WAF_END
     *                       AFTER_ON_REQ_WAF → BEFORE_RUN_WAF_END */
    PENDING_WAF_END,

    WAF_END_BLOCK_COMMIT,

    /* Set on the thread of the final WAF run, after the WAF has run, or
     * directly on the main thread, if we could not submit the WAF final task.
     * Incoming transitions: PENDING_WAF_END → AFTER_RUN_WAF_END */
    AFTER_RUN_WAF_END,

    // possible final states:
    // - DISABLED
    // - AFTER_BEGIN_WAF_BLOCK (blocked on 1st WAF run)
    // - AFTER_ON_REQ_WAF_BLOCK (blocked on req body WAF run)
    // - BEFORE_RUN_WAF_END (submission of last WAF run failed)
    // - AFTER_RUN_WAF_END (WAF finished)
  };

  std::unique_ptr<std::atomic<stage>> stage_;
  [[maybe_unused]] stage transition_to_stage(stage stage) {
    stage_->store(stage, std::memory_order_release);
    return stage;
  }
  [[maybe_unused]] bool checked_transition_to_stage(stage from, stage to) {
    return stage_->compare_exchange_strong(from, to, std::memory_order_acq_rel);
  }

  std::shared_ptr<OwnedDdwafHandle> waf_handle_;
  std::vector<OwnedDdwafResult> results_;
  OwnedDdwafContext ctx_{nullptr};
  DdwafMemres memres_;
  std::optional<std::string> client_ip_;

  static inline constexpr std::size_t kMaxFilterData = 40 * 1024;
  static inline constexpr std::size_t kDefaultMaxSavedOutputData = 256 * 1024;
  std::size_t max_saved_output_data_{kDefaultMaxSavedOutputData};

  struct FilterCtx {
    ngx_chain_t *out;  // the buffered request or response body
    ngx_chain_t **out_latest{&out};
    std::size_t out_total;
    std::size_t copied_total;
    bool found_last;

    void clear(ngx_pool_t &pool) noexcept;
    void replace_out(ngx_chain_t *new_out) noexcept;
  };
  FilterCtx filter_ctx_{};         // for request body
  FilterCtx header_filter_ctx_{};  // for the header data
  FilterCtx out_filter_ctx_{};     // for response body

  static ngx_int_t buffer_chain(FilterCtx &filter_ctx, ngx_pool_t &pool,
                                ngx_chain_t const *in, bool consume) noexcept;

  ngx_http_event_handler_pt prev_req_write_evt_handler_;
  static void drain_buffered_data_write_handler(ngx_http_request_t *r) noexcept;

 public:
  ngx_int_t buffer_header_output(ngx_pool_t &pool, ngx_chain_t *chain) noexcept;
  ngx_int_t send_buffered_header(ngx_http_request_t &request) noexcept;

  void prepare_drain_buffered_header(ngx_http_request_t &request) noexcept;
};

}  // namespace datadog::nginx::security
