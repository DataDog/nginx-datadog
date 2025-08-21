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
#include "ddwaf_req.h"
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

class Context {
  Context(std::shared_ptr<OwnedDdwafHandle> waf_handle,
          bool apm_tracing_enabled);

 public:
  ~Context();

  // returns a new context or an empty unique_ptr if the waf is not active
  static std::unique_ptr<Context> maybe_create(
      std::optional<std::size_t> max_saved_output_data,
      bool apm_tracing_enabled);

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

  bool keep_span() const noexcept;

 private:
  bool do_on_request_start(ngx_http_request_t &request, dd::Span &span);
  ngx_int_t do_request_body_filter(ngx_http_request_t &request,
                                   ngx_chain_t *chain, dd::Span &span);
  ngx_int_t do_header_filter(ngx_http_request_t &request, dd::Span &span);
  ngx_int_t do_output_body_filter(ngx_http_request_t &request,
                                  ngx_chain_t *chain, dd::Span &span);
  void do_on_main_log_request(ngx_http_request_t &request, dd::Span &span);

  void report_matches(ngx_http_request_t &request, dd::Span &span);
  void report_client_ip(dd::Span &span) const;

  // clang-format off
  /*
    Initial path:
    ┌───────┐   on_request_start   ┌──────────────────┐  1st WAF task   ┌─────────────────┐
    │ START │ ───────────────────► │ ENTERED_ON_START │ ──────────────► │ AFTER_BEGIN_WAF │
    └───────┘                      └──────────────────┘                 └───────┬───┬─────┘
                                             │                                  │   │
                                (no waf_pool)│                                  │   └──────────► To req/resp body proc
                                             ▼                                  │
                                         ┌──────────┐                           │(WAF blocked)
                                         │ DISABLED │                           │
                                         └──────────┘                           ▼
                                                                  ┌───────────────────────┐
                                                                  │ AFTER_BEGIN_WAF_BLOCK │ (Terminal)
                                                                  └───────────────────────┘

    Request Body Processing (from AFTER_BEGIN_WAF):
    ┌─────────────────┐
    │ AFTER_BEGIN_WAF │ ─────► (no parseable) To Response Processing
    └──┬────┬─────────┘
       |    |
       |    |   ┌────────────────────────────────┐
       |    └ ► │ COLLECTING_ON_REQ_DATA_PREREAD │
       |        └────────┬───────────────────────┘
       |                 |
       ▼                 ▼
    ┌────────────────────────┐
    │ COLLECTING_ON_REQ_DATA │
    └───┬─────┬──────────────┘
        |     │              │ (enough/no more data)
        |     |              ▼
        |     │  ┌──────────────────────┐
        |     └─►│ SUSPENDED_ON_REQ_WAF │
        |        └──────────────────────┘
        |                │(no block)  │ (blocked)
        |                │            ▼
        | (no data/      │    ┌────────────────────────┐
        |  failed waf    |    | AFTER_ON_REQ_WAF_BLOCK │ (Terminal)
        |  submission)   |    └────────────────────────┘
        |                │
        |                ▼
        |        ┌──────────────────┐
        └──────► │ AFTER_ON_REQ_WAF │ ───► To Response Processing
                 └──────────────────┘

    Response Processing (from AFTER_BEGIN_WAF or AFTER_ON_REQ_WAF):
    (header filter)
            │                       ┌─────────────────────────┐
            ├─(needs resp body)───► │ COLLECTING_ON_RESP_DATA │
            │                       └─────────┬───────────────┘
            │                                 │ (body filter: enough data)
            │                                 ▼
            |                         ┌─────────────────┐
            └─(no resp body needed)─► │ PENDING_WAF_END │
                                      └───────┬─────────┘
                                              │
                         ┌────────────────────┴──────────────────┐
                         │ (final WAF, no block)                 │ (final WAF, block)
                         ▼                                       ▼
                ┌───────────────────┐                 ┌──────────────────────┐
                │ AFTER_RUN_WAF_END │ (Terminal)      │ WAF_END_BLOCK_COMMIT │
                └───────────────────┘                 └──────────┬───────────┘
                         ▲                                       │
                         └── ( block response sent)──────────────┘
  */
  // clang-format on
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

    /* Set on the header filter, if we need to collect response body data
     * before doing the final WAF run.
     * Incoming transitions: AFTER_BEGIN_WAF → COLLECTIING_ON_RESP_DATA
     *                       AFTER_ON_REQ_WAF → COLLECTING_ON_RESP_DATA */
    COLLECTING_ON_RESP_DATA,

    /* Set on the header filter, if we don't need to collect the body response,
     * or on the body filter, once we have enough data.
     * Incoming transitions: AFTER_BEGIN_WAF → BEFORE_RUN_WAF_END
     *                       AFTER_ON_REQ_WAF → BEFORE_RUN_WAF_END
     *                       COLLECTING_ON_RESP_DATA → BEFORE_RUN_WAF_END */
    PENDING_WAF_END,

    /* Set on the main thread, on the completion handler of the final waf run
     * task.
     * Incoming transitions: PENDING_WAF_END → WAF_END_BLOCK_COMMIT */
    WAF_END_BLOCK_COMMIT,

    /* Set on the completion handler of the final WAF run, after the WAF has
     * run, in exceptional circumstances, in the header filter (downstream
     * filter failed), in the body filter, after committing the block response,
     * or on the completion handler of the final waf run task, if we're not
     * to block.
     * Incoming transitions: PENDING_WAF_END → AFTER_RUN_WAF_END
     *                       WAF_END_BLOCK_COMMIT → AFTER_RUN_WAF_END
     *                       AFTER_BEGIN_WAF → AFTER_RUN_WAF_END (rare)
     *                       AFTER_ON_REQ_WAF → AFTER_RUN_WAF_END (rare) */
    AFTER_RUN_WAF_END,

    // possible final states:
    // - DISABLED
    // - AFTER_BEGIN_WAF_BLOCK (blocked on 1st WAF run)
    // - AFTER_ON_REQ_WAF_BLOCK (blocked on req body WAF run)
    // - AFTER_RUN_WAF_END (WAF finished)
  };

  static ngx_str_t *to_ngx_str(stage st) {
    switch (st) {
      case stage::DISABLED:
        static ngx_str_t disabled = ngx_string("DISABLED");
        return &disabled;
      case stage::START:
        static ngx_str_t start = ngx_string("START");
        return &start;
      case stage::ENTERED_ON_START:
        static ngx_str_t entered_on_start = ngx_string("ENTERED_ON_START");
        return &entered_on_start;
      case stage::AFTER_BEGIN_WAF:
        static ngx_str_t after_begin_waf = ngx_string("AFTER_BEGIN_WAF");
        return &after_begin_waf;
      case stage::AFTER_BEGIN_WAF_BLOCK:
        static ngx_str_t after_begin_waf_block =
            ngx_string("AFTER_BEGIN_WAF_BLOCK");
        return &after_begin_waf_block;
      case stage::COLLECTING_ON_REQ_DATA_PREREAD:
        static ngx_str_t collecting_on_req_data_preread =
            ngx_string("COLLECTING_ON_REQ_DATA_PREREAD");
        return &collecting_on_req_data_preread;
      case stage::COLLECTING_ON_REQ_DATA:
        static ngx_str_t collecting_on_req_data =
            ngx_string("COLLECTING_ON_REQ_DATA");
        return &collecting_on_req_data;
      case stage::SUSPENDED_ON_REQ_WAF:
        static ngx_str_t suspended_on_req_waf =
            ngx_string("SUSPENDED_ON_REQ_WAF");
        return &suspended_on_req_waf;
      case stage::AFTER_ON_REQ_WAF:
        static ngx_str_t after_on_req_waf = ngx_string("AFTER_ON_REQ_WAF");
        return &after_on_req_waf;
      case stage::AFTER_ON_REQ_WAF_BLOCK:
        static ngx_str_t after_on_req_waf_block =
            ngx_string("AFTER_ON_REQ_WAF_BLOCK");
        return &after_on_req_waf_block;
      case stage::COLLECTING_ON_RESP_DATA:
        static ngx_str_t collecting_on_resp_data =
            ngx_string("COLLECTING_ON_RESP_DATA");
        return &collecting_on_resp_data;
      case stage::PENDING_WAF_END:
        static ngx_str_t pending_waf_end = ngx_string("PENDING_WAF_END");
        return &pending_waf_end;
      case stage::WAF_END_BLOCK_COMMIT:
        static ngx_str_t waf_end_block_commit =
            ngx_string("WAF_END_BLOCK_COMMIT");
        return &waf_end_block_commit;
      case stage::AFTER_RUN_WAF_END:
        static ngx_str_t after_run_waf_end = ngx_string("AFTER_RUN_WAF_END");
        return &after_run_waf_end;
      default:
        static ngx_str_t unknown = ngx_string("UNKNOWN");
        return &unknown;
    }
  }

  std::unique_ptr<std::atomic<stage>> stage_;
  [[maybe_unused]] stage transition_to_stage(stage stage) {
    stage_->store(stage, std::memory_order_release);
    return stage;
  }
  [[maybe_unused]] bool checked_transition_to_stage(stage from, stage to) {
    return stage_->compare_exchange_strong(from, to, std::memory_order_acq_rel);
  }

  std::unique_ptr<DdwafContext> waf_ctx_;
  DdwafMemres memres_;
  std::optional<std::string> client_ip_;

  // max request or response body data we parse
  static inline constexpr std::size_t kMaxFilterData = 40 * 1024;
  static inline constexpr std::size_t kDefaultMaxSavedOutputData = 256 * 1024;

  bool waf_send_resp_body_{true};
  std::size_t max_saved_output_data_{kDefaultMaxSavedOutputData};

  bool apm_tracing_enabled_;

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
  bool header_only_{false};        // HEAD requests

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
