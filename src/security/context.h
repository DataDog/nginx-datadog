#pragma once

#include <datadog/span.h>
#include <ddwaf.h>

#include <atomic>
#include <cstddef>
#include <optional>
#include <stdexcept>

#include "../dd.h"
#include "collection.h"

extern "C" {
#include <nginx.h>
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
}

namespace datadog {
namespace nginx {
namespace security {

template <typename T, typename FreeFunc>
struct freeable_resource {
  static_assert(std::is_standard_layout<T>::value, "T must be a POD type");

  T resource;
  explicit freeable_resource(const T resource) : resource{resource} {}

  freeable_resource(freeable_resource &&other) noexcept {
    resource = other.resource;
    other.resource = {};
  };
  freeable_resource &operator=(freeable_resource &&other) noexcept {
    if (this != &other) {
      resource = other.resource;
      other.resource = {};
    }
    return *this;
  }
  freeable_resource(const freeable_resource &other) = delete;
  freeable_resource &operator=(const freeable_resource &other) = delete;
  T &operator*() { return resource; }

  ~freeable_resource() { FreeFunc()(resource); }
};

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
  owned_ddwaf_context(std::nullptr_t) : freeable_resource{nullptr} {}
  owned_ddwaf_context &operator=(ddwaf_context ctx) {
    if (resource) {
      ddwaf_context_destroy(resource);
    }
    resource = ctx;
    return *this;
  }
};

class context {
 public:
  context();

  bool on_request_start(ngx_http_request_t &request, dd::Span &span) noexcept;
  void on_request_end(const ngx_http_request_t &request,
                      dd::Span &span) noexcept;

 private:
  bool do_on_request_start(ngx_http_request_t &request, dd::Span &span);
  void do_on_request_end(const ngx_http_request_t &request, dd::Span &span);

  // runs on a separate thread
  void run_waf_start(const ngx_http_request_t &request, dd::Span &span);

  std::vector<owned_ddwaf_result> results_;
  owned_ddwaf_context ctx_{nullptr};
  ddwaf_memres memres_;

  enum class stage { start, after_begin_waf, after_report };
  std::unique_ptr<std::atomic<stage>> stage_;
};

}  // namespace security
}  // namespace nginx
}  // namespace datadog
