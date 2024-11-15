#pragma once

#include <datadog/span.h>
#include <ddwaf.h>
#include <sys/types.h>

#include <atomic>
#include <cstddef>
#include <memory>
#include <optional>
#include <stdexcept>

#include "../dd.h"
#include "blocking.h"
#include "collection.h"
#include "ddwaf_obj.h"
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
  static std::unique_ptr<Context> maybe_create();

  ngx_int_t request_body_filter(ngx_http_request_t &request, ngx_chain_t *chain,
                                dd::Span &span) noexcept;

  bool on_request_start(ngx_http_request_t &request, dd::Span &span) noexcept;

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

  std::optional<BlockSpecification> run_waf_end(ngx_http_request_t &request,
                                                dd::Span &span);

 private:
  bool do_on_request_start(ngx_http_request_t &request, dd::Span &span);
  ngx_int_t do_request_body_filter(ngx_http_request_t &request,
                                   ngx_chain_t *chain, dd::Span &span);
  ngx_int_t do_output_body_filter(ngx_http_request_t &request,
                                  ngx_chain_t *chain, dd::Span &span);
  void do_on_main_log_request(ngx_http_request_t &request, dd::Span &span);

  bool has_matches() const noexcept;
  void report_matches(ngx_http_request_t &request, dd::Span &span);
  ngx_int_t buffer_chain(ngx_pool_t &pool, ngx_chain_t *in, bool consume);

  enum class stage {
    DISABLED,
    START,
    ENTERED_ON_START,
    AFTER_BEGIN_WAF,
    AFTER_BEGIN_WAF_BLOCK,  // in this case we won't run the waf at the end
    COLLECTING_ON_REQ_DATA_PREREAD,
    COLLECTING_ON_REQ_DATA,
    SUSPENDED_ON_REQ_WAF,
    AFTER_ON_REQ_WAF,
    AFTER_ON_REQ_WAF_BLOCK,
    BEFORE_RUN_WAF_END,
    AFTER_RUN_WAF_END,
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

  static inline constexpr auto kMaxFilterData = 40 * 1024;

  struct FilterCtx {
    ngx_chain_t *out;  // the buffered request body
    ngx_chain_t **out_last{&out};
    std::size_t out_total;
    bool found_last;
  };
  FilterCtx filter_ctx_{};
};

}  // namespace datadog::nginx::security
