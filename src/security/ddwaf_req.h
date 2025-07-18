#pragma once

#include <ddwaf.h>

#include <functional>
#include <vector>

#include "blocking.h"
#include "library.h"
#include "util.h"

namespace datadog::nginx::security {

class DdwafContext {
 public:
  explicit DdwafContext(std::shared_ptr<OwnedDdwafHandle>& handle);

  DdwafContext(DdwafContext&&) = delete;
  DdwafContext& operator=(DdwafContext&&) = delete;
  DdwafContext(const DdwafContext&) = delete;
  DdwafContext& operator=(const DdwafContext&) = delete;

  struct WafRunResult {
    DDWAF_RET_CODE ret_code;
    std::optional<BlockSpecification> block_spec;
  };

  WafRunResult run(ddwaf_object& persistent_data);

  bool has_matches() const { return !results_.empty(); }

  // if there are matches, calls the function with the desired contents for
  // _dd.appsec.json and returns true. O/wise returns false.
  bool report_matches(const std::function<void(std::string_view)>& f);

 private:
  struct DdwafContextFreeFunctor {
    void operator()(ddwaf_context ctx) { ddwaf_context_destroy(ctx); }
  };
  struct OwnedDdwafContext
      : FreeableResource<ddwaf_context, DdwafContextFreeFunctor> {
    using FreeableResource::FreeableResource;
    explicit OwnedDdwafContext(std::nullptr_t) : FreeableResource{nullptr} {}
    OwnedDdwafContext& operator=(ddwaf_context ctx) {
      if (resource) {
        ddwaf_context_destroy(resource);
      }
      resource = ctx;
      return *this;
    }
  };

  struct DdwafResultFreeFunctor {
    void operator()(ddwaf_result result) { ddwaf_result_free(&result); }
  };
  struct OwnedDdwafResult
      : FreeableResource<ddwaf_result, DdwafResultFreeFunctor> {
    using FreeableResource::FreeableResource;
    explicit OwnedDdwafResult(ddwaf_result result) : FreeableResource{result} {}
  };

  OwnedDdwafContext ctx_;
  std::vector<OwnedDdwafResult> results_;
};
}  // namespace datadog::nginx::security
