#pragma once

#include <ddwaf.h>

#include <functional>
#include <vector>

#include "blocking.h"
#include "library.h"
#include "security/ddwaf_obj.h"
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

  WafRunResult run(ngx_log_t& log, ddwaf_object& persistent_data);

  bool has_matches() const;
  bool keep() const { return keep_; }
  const std::unordered_map<std::string_view, std::string>& collected_tags()
      const {
    return collected_tags_;
  }
  const std::unordered_map<std::string_view, double>& collected_metrics()
      const {
    return collected_metrics_;
  }

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

  OwnedDdwafContext ctx_;
  bool keep_{false};
  std::vector<libddwaf_owned_ddwaf_obj<ddwaf_map_obj>> results_;
  // string_views are backed by the results_ vector
  std::unordered_map<std::string_view, std::string> collected_tags_;
  std::unordered_map<std::string_view, double> collected_metrics_;
};
}  // namespace datadog::nginx::security
