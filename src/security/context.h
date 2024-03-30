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

struct ddwaf_result_free_functor {
  void operator()(ddwaf_result &res) { ddwaf_result_free(&res); }
};
struct owned_ddwaf_result
    : freeable_resource<ddwaf_result, ddwaf_result_free_functor> {
  using freeable_resource::freeable_resource;
};

struct ddwaf_context_free_functor {
  void operator()(ddwaf_context ctx) { ddwaf_context_destroy(ctx); }
};
struct owned_ddwaf_context
    : freeable_resource<ddwaf_context, ddwaf_context_free_functor> {
  using freeable_resource::freeable_resource;
  explicit owned_ddwaf_context(std::nullptr_t) : freeable_resource{nullptr} {}
  owned_ddwaf_context &operator=(ddwaf_context ctx) {
    if (resource) {
      ddwaf_context_destroy(resource);
    }
    resource = ctx;
    return *this;
  }
};

class context {
  context(std::shared_ptr<waf_handle> waf_handle);

 public:
  // returns a new context or an empty unique_ptr if the waf is not active
  static std::unique_ptr<context> maybe_create();

  bool on_request_start(ngx_http_request_t &request, dd::Span &span) noexcept;
  ngx_int_t output_header_filter(ngx_http_request_t &request,
                                 dd::Span &span) noexcept;

  // runs on a separate thread; returns whether it blocked
  std::optional<block_spec> run_waf_start(ngx_http_request_t &request,
                                          dd::Span &span);
  std::optional<block_spec> run_waf_end(ngx_http_request_t &request,
                                        dd::Span &span);

 private:
  bool do_on_request_start(ngx_http_request_t &request, dd::Span &span);
  ngx_int_t do_output_header_filter(ngx_http_request_t &request,
                                    dd::Span &span);

  std::shared_ptr<waf_handle> waf_handle_;
  std::vector<owned_ddwaf_result> results_;
  owned_ddwaf_context ctx_{nullptr};
  ddwaf_memres memres_;

  enum class stage {
    disabled,
    start,
    after_begin_waf,
    after_begin_waf_block,  // in this case we won't run the waf at the end
    after_report
  };
  std::unique_ptr<std::atomic<stage>> stage_;
};

}  // namespace datadog::nginx::security
