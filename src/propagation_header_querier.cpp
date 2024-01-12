#include "propagation_header_querier.h"

#include <datadog/dict_writer.h>
#include <datadog/injection_options.h>
#include <datadog/sampling_decision.h>
#include <datadog/trace_segment.h>

#include <algorithm>
#include <cassert>
#include <iterator>
#include <new>
#include <string_view>

#include "dd.h"
#include "ngx_http_datadog_module.h"
#include "string_util.h"

extern "C" {
#include <ngx_http_config.h>
}

namespace datadog {
namespace nginx {

ngx_str_t PropagationHeaderQuerier::lookup_value(ngx_http_request_t* request, const dd::Span& span,
                                                 std::string_view key) {
  if (&span != values_span_) {
    expand_values(request, span);
  }

  const auto found = span_context_expansion_.find(std::string{key});
  if (found != span_context_expansion_.end()) {
    return to_ngx_str(found->second);
  }

  // This will prevent the header from being added to the proxied request.
  return ngx_str_t();
}

namespace {

class SpanContextValueWriter : public dd::DictWriter {
  std::unordered_map<std::string, std::string>* span_context_expansion_;
  std::string* buffer_;

 public:
  explicit SpanContextValueWriter(
      std::unordered_map<std::string, std::string>& span_context_expansion, std::string& buffer)
      : span_context_expansion_(&span_context_expansion), buffer_(&buffer) {}

  void set(std::string_view key, std::string_view value) override {
    buffer_->clear();
    std::transform(key.begin(), key.end(), std::back_inserter(*buffer_), header_transform_char);
    span_context_expansion_->insert_or_assign(*buffer_, value);
  }
};

}  // namespace

void PropagationHeaderQuerier::expand_values(ngx_http_request_t* request, const dd::Span& span) {
  dd::InjectionOptions options;
  auto loc_conf = static_cast<datadog_loc_conf_t*>(
      ngx_http_get_module_loc_conf(request, ngx_http_datadog_module));
  // `loc_conf->sampling_delegation_script` tells us whether sampling delegation
  // is configured for this `location`. First, though, we must check whether
  // we're a subrequest (e.g. an authentication request), and consider the
  // sampling delegation script only if sampling delegation is allowed for
  // subrequests in this `location`.
  ngx_str_t delegation;  // whether to delegate; filled out below
  const bool is_subrequest = request->parent != nullptr;
  if (is_subrequest) {
    const ngx_str_t subrequest_delegation =
        loc_conf->allow_sampling_delegation_in_subrequests_script.run(request);
    if (str(subrequest_delegation) == "on") {
      delegation = loc_conf->sampling_delegation_script.run(request);
    } else if (str(subrequest_delegation) == "off") {
      delegation = ngx_string("off");
    } else {
      const auto& directive = loc_conf->allow_sampling_delegation_in_subrequests_directive;
      ngx_log_error(NGX_LOG_ERR, request->connection->log, 0,
                    "Condition expression for %V directive at %V:%d evaluated to unexpected value "
                    "\"%V\". Expected \"on\" or \"off\". Proceeding as if the value were \"off\".",
                    &directive.directive_name, &directive.file_name, directive.line,
                    &subrequest_delegation);
      delegation = ngx_string("off");
    }
  } else {
    delegation = loc_conf->sampling_delegation_script.run(request);
  }

  // There's one last piece of logic. If the trace segment that `span` is part
  // of has already successfully delegated the sampling decision (to a past
  // subrequest), then don't delegate again.
  const dd::Optional<dd::SamplingDecision> previous_decision =
      span.trace_segment().sampling_decision();
  const bool already_delegated =
      previous_decision && previous_decision->origin == dd::SamplingDecision::Origin::DELEGATED;

  if (already_delegated) {
    options.delegate_sampling_decision = false;
  } else if (str(delegation) == "on") {
    options.delegate_sampling_decision = true;
  } else if (str(delegation) == "off") {
    options.delegate_sampling_decision = false;
  } else if (str(delegation) == "") {
    // Leave `options.delegation_sampling_decision` null, so that the tracer
    // configuration will be used instead.
  } else {
    const auto& directive = loc_conf->sampling_delegation_directive;
    ngx_log_error(
        NGX_LOG_ERR, request->connection->log, 0,
        "Condition expression for %V directive at %V:%d evaluated to unexpected value "
        "\"%V\". Expected \"on\" or \"off\". Proceeding as if the directive were absent.",
        &directive.directive_name, &directive.file_name, directive.line, &delegation);
  }

  values_span_ = &span;
  span_context_expansion_.clear();
  SpanContextValueWriter writer{span_context_expansion_, buffer_};
  span.inject(writer, options);
}

}  // namespace nginx
}  // namespace datadog
