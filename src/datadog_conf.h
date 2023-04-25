#pragma once

#include "dd.h"
#include "ngx_script.h"

extern "C" {
#include <nginx.h>
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
}

#include <datadog/propagation_style.h>
#include <datadog/trace_sampler_config.h>

#include <string>
#include <vector>

namespace datadog {
namespace nginx {

struct datadog_tag_t {
  NgxScript key_script;
  NgxScript value_script;
};

struct conf_directive_source_location_t {
  ngx_str_t file_name;       // e.g. "nginx.conf"
  ngx_uint_t line;           // line number within the file `file_name`
  ngx_str_t directive_name;  // e.g. "proxy_pass"
};

bool operator==(const conf_directive_source_location_t &,
                const conf_directive_source_location_t &);

struct environment_variable_t {
  std::string name;
  std::string value;
};

struct configured_value_t {
  conf_directive_source_location_t location;
  std::string value;
};

struct sampling_rule_t {
  // If the corresponding `datadog_sample_rate` directive was in the `http`
  // block, then `*depth == 0`. If `server` block, then `*depth == 1`. If
  // `location` block, then `*depth == 2`.
  // `depth` is used to sort rules from "most specific to least specific," i.e.
  // sort by `depth` descending.
  // `depth` is a pointer to a data member in `datadog_loc_conf_t`. The value
  // of `*depth` is not known until location configurations are merged into
  // each other, which happens after the `datadog_sample_rate` directive
  // handler that produced this `sampling_rule_t`.
  int *depth = nullptr;
  // `rule` targets the sample rate and source location of a particular
  // `datadog_sample_rate` directive.
  dd::TraceSamplerConfig::Rule rule;
};

struct datadog_main_conf_t {
  ngx_array_t *tags;
  // `are_propagation_styles_locked` is whether the tracer's propagation styles
  // have been set, either by an explicit `datadog_propagation_styles`
  // directive, or implicitly to a default configuration by another directive.
  // The propagation styles must be known whenever we encounter a `proxy_pass`
  // or similar directive.
  bool are_propagation_styles_locked;
  // `propagation_styles_source_location` is the source location of the
  // configuration directive that caused the propagation styles to be locked.
  // The `datadog_propagation_styles` directive causes the styles to be locked,
  // but there are other directives that cause a default configuration to be
  // used if no other configuration has yet been loaded.
  // The purpose of `propagation_styles_source_location` is to enable the error
  // diagnostic:
  // > Propagation styles already set to default values by
  // > [[source location]].  The datadog_propagation_styles directive must
  // > appear before the first [[directive name]].
  conf_directive_source_location_t propagation_styles_source_location;
  // `are_log_formats_defined` is whether we have already injected `log_format`
  // directives into the configuration.  The directives define Datadog-specific
  // access log formats; one of which will override nginx's default.
  // `are_log_formats_defined` allows us to ensure that the log formats are
  // defined exactly once, even though they may be defined in multiple contexts
  // (e.g. before the first `server` block, before the first `access_log`
  // directive).
  bool are_log_formats_defined;
  std::vector<std::string_view> span_context_keys;
  // This module automates the forwarding of the environment variables in
  // `TracingLibrary::environment_variable_names()`. Rather than injecting
  // `env` directives into the configuration, or mucking around with the core
  // module configuration, instead we grab the values from the environment
  // of the master process and apply them later in the worker processes after
  // `fork()`.
  std::vector<environment_variable_t> environment_variables;
  // If `propagation_styles` is empty, then use the defaults instead.
  // `propagation_styles` is populated by the "datadog_propagation_styles"
  // configuration directive.
  std::vector<dd::PropagationStyle> propagation_styles;
  // `sampling_rules` contains one sampling rule per `datadog_sample_rate` in
  // the nginx configuration. Each rule is associated with its "depth" in the
  // configuration, so that the rules can be sorted before use by the tracer
  // config.
  std::vector<sampling_rule_t> sampling_rules;
  // `service_name` is set by the `datadog_service_name` directive.
  std::optional<configured_value_t> service_name;
  // `service_type` is set by the `datadog_service_type` directive.
  std::optional<configured_value_t> service_type;
  // `environment` is set by the `datadog_environment` directive.
  std::optional<configured_value_t> environment;
  // `agent_url` is set by the `datadog_agent_url` directive.
  std::optional<configured_value_t> agent_url;
};

struct datadog_sample_rate_condition_t {
  // If `condition` evaluates to "on" for a request, then it is active for that
  // request. If it evaluates to "off", then it's inactive for that request. If
  // it evaluates to some other value, then an error is printed and it defaults
  // to "off".
  NgxScript condition;
  // `directive` is the location of the associated "sample_rate" directive in
  // the configuration file.
  conf_directive_source_location_t directive;
  // If two `directive` are the same, because two `datadog_sample_rate`
  // directives are on the same line in the same file, e.g.
  //
  //     datadog_sample_rate 0.5 "$maybe"; datadog_sample_rate 1.0;
  //
  // then `same_line_index` is the zero-based index of the directive among those
  // on the same line. In the example above, the "0.5" sample rate would have
  // `same_line_index == 0`, while the "1.0" sample rate would have
  // `same_line_index == 1`.
  // If `directive` is unique, then `same_line_index == 0`.
  int same_line_index;

  // Return the name of the span tag that will be used by sampling rules to
  // match this `datadog_sample_rate` directive. It's a constant.
  std::string tag_name() const;
  // Return the value of the span tag that will be used by sampling rules to
  // match this `datadog_sample_rate` directive. It depends on `directive` and
  // `same_line_index`.
  std::string tag_value() const;
};

struct datadog_loc_conf_t {
  ngx_flag_t enable = NGX_CONF_UNSET;
  ngx_flag_t enable_locations = NGX_CONF_UNSET;
  NgxScript operation_name_script;
  NgxScript loc_operation_name_script;
  NgxScript resource_name_script;
  NgxScript loc_resource_name_script;
  ngx_flag_t trust_incoming_span = NGX_CONF_UNSET;
  ngx_array_t *tags;
  // `response_info_script` is a script that can contain variables that refer
  // to HTTP response headers.  The headers might be relevant in the future.
  // Currently `response_info_script` is not used.
  NgxScript response_info_script;
  // `proxy_directive` is the name of the configuration directive used to proxy
  // requests at this location, i.e. `proxy_pass`, `grpc_pass`, or
  // `fastcgi_pass`.  If this location does not have such a directive directly
  // within it (as opposed to in a location nested within it), then
  // `proxy_directive` is empty.
  ngx_str_t proxy_directive;
  // `parent` is the parent context (e.g. the `server` to this `location`), or
  // `nullptr` if this context has not parent.
  datadog_loc_conf_t *parent;
  // `sample_rates` contains one entry per `sample_rate` directive in this
  // location. Entries for enclosing contexts can be accessed through `parent`.
  std::vector<datadog_sample_rate_condition_t> sample_rates;
  // `depth` is how far nested this configuration is from its oldest ancestor.
  // The oldest ancestor (the `http` block) has `depth` zero. Each subsequent
  // generation has the `depth` of its parent plus one.
  int depth;
};

}  // namespace nginx
}  // namespace datadog
