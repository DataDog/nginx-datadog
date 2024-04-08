#pragma once

#include <datadog/span.h>
#include <ddwaf.h>

#include <atomic>
#include <cstddef>
#include <memory>
#include <optional>
#include <stdexcept>

#include "../dd.h"
#include "blocking.h"
#include "collection.h"
#include "library.h"
#include "util.h"

extern "C" {
#include <nginx.h>
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
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
  Context(std::shared_ptr<WafHandle> waf_handle);

 public:
  // returns a new context or an empty unique_ptr if the waf is not active
  static std::unique_ptr<Context> maybe_create();

  bool on_request_start(ngx_http_request_t &request, dd::Span &span) noexcept;
  ngx_int_t output_body_filter(ngx_http_request_t &request, ngx_chain_t *chain,
                               dd::Span &span) noexcept;

  // runs on a separate thread; returns whether it blocked
  std::optional<BlockSpecification> run_waf_start(ngx_http_request_t &request,
                                                  dd::Span &span);
  std::optional<BlockSpecification> run_waf_end(ngx_http_request_t &request,
                                                dd::Span &span);

 private:
  bool do_on_request_start(ngx_http_request_t &request, dd::Span &span);
  ngx_int_t do_output_body_filter(ngx_http_request_t &request,
                                  ngx_chain_t *chain, dd::Span &span);

  std::shared_ptr<WafHandle> waf_handle_;
  std::vector<OwnedDdwafResult> results_;
  OwnedDdwafContext ctx_{nullptr};
  DdwafMemres memres_;

  enum class stage {
    DISABLED,
    START,
    AFTER_BEGIN_WAF,
    AFTER_BEGIN_WAF_BLOCK,  // in this case we won't run the waf at the end
    BEFORE_RUN_WAF_END,
    AFTER_REPORT,
  };
  std::unique_ptr<std::atomic<stage>> stage_;
};

}  // namespace datadog::nginx::security
