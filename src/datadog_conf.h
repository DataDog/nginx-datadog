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
#ifdef WITH_RUM
#include <injectbrowsersdk.h>
#endif

#include <string>
#include <unordered_map>
#include <vector>

#define DD_NGX_CONF_COMPLEX_UNSET \
  (ngx_http_complex_value_t *)NGX_CONF_UNSET_PTR;

namespace datadog {
namespace nginx {

struct datadog_tag_t {
  ngx_http_complex_value_t *key;
  ngx_http_complex_value_t *value;
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
  // ngx_array_t *tags;
  std::unordered_map<std::string, ngx_http_complex_value_t *> tags;
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
  // `agent_url` is set by the `datadog_agent_url` directive.
  std::optional<std::string> agent_url;

#ifdef WITH_WAF
  // DD_APPSEC_ENABLED
  ngx_flag_t appsec_enabled{NGX_CONF_UNSET};

  // DD_APPSEC_RULES
  ngx_str_t appsec_ruleset_file{};

  // DD_APPSEC_HTTP_BLOCKED_TEMPLATE_JSON
  ngx_str_t appsec_http_blocked_template_json{};

  // DD_APPSEC_HTTP_BLOCKED_TEMPLATE_HTML
  ngx_str_t appsec_http_blocked_template_html{};

  // DD_TRACE_CLIENT_IP_HEADER
  ngx_str_t custom_client_ip_header{};

  // DD_APPSEC_WAF_TIMEOUT (default: 0.1 s), in microseconds
  // While the environment variable is specified in microseconds, we store
  // the value in milliseconds for easier use with nginx's time handling.
  // The default value is not set to 100 to detect when the value is unset
  // When specified in nginx configuration, follows the usual pattern for such
  // settings in nginx (e.g. 100ms)
  ngx_msec_t appsec_waf_timeout_ms{NGX_CONF_UNSET_MSEC};

  // DD_APPSEC_OBFUSCATION_PARAMETER_KEY_REGEXP
  ngx_str_t appsec_obfuscation_key_regex = ngx_null_string;

  // DD_APPSEC_OBFUSCATION_PARAMETER_VALUE_REGEXP
  ngx_str_t appsec_obfuscation_value_regex = ngx_null_string;

  // (only nginx configuration: datadog_appsec_max_saved_output_data)
  // How much data we're willing to copy while waiting for the final WAF run
  // before we stall the output filter chain with busy buffers
  std::size_t appsec_max_saved_output_data{NGX_CONF_UNSET_SIZE};

  // TODO: missing settings and their functionality
  // DD_TRACE_CLIENT_IP_RESOLVER_ENABLED (whether to collect headers and run the
  // client ip resolution. Also requires AppSec to be enabled or
  // clientIpEnabled)
  // DD_TRACE_CLIENT_IP_ENABLED (client ip without appsec)
  // DD_APPSEC_WAF_METRICS
  // DD_APPSEC_REPORT_TIMEOUT
#endif
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
  ngx_flag_t enable_tracing = NGX_CONF_UNSET;
  ngx_flag_t enable_locations = NGX_CONF_UNSET;
  ngx_http_complex_value_t *operation_name_script = DD_NGX_CONF_COMPLEX_UNSET;
  ngx_http_complex_value_t *loc_operation_name_script =
      DD_NGX_CONF_COMPLEX_UNSET;
  ngx_http_complex_value_t *resource_name_script = DD_NGX_CONF_COMPLEX_UNSET;
  ngx_http_complex_value_t *loc_resource_name_script =
      DD_NGX_CONF_COMPLEX_UNSET;
  ngx_flag_t trust_incoming_span = NGX_CONF_UNSET;
  // `service_name` is set by the `datadog_service_name` directive.
  ngx_http_complex_value_t *service_name = DD_NGX_CONF_COMPLEX_UNSET;
  // `service_env` is set by the `datadog_environment` directive.
  ngx_http_complex_value_t *service_env = DD_NGX_CONF_COMPLEX_UNSET;
  // `service_version` is set by the `datadog_version` directive.
  ngx_http_complex_value_t *service_version = DD_NGX_CONF_COMPLEX_UNSET;
  // ngx_array_t *tags;
  std::unordered_map<std::string, ngx_http_complex_value_t *> tags;
  // `parent` is the parent context (e.g. the `server` to this `location`), or
  // `nullptr` if this context has no parent.
  datadog_loc_conf_t *parent;
  // `sample_rates` contains one entry per `sample_rate` directive in this
  // location. Entries for enclosing contexts can be accessed through `parent`.
  std::vector<datadog_sample_rate_condition_t> sample_rates;
  // `depth` is how far nested this configuration is from its oldest ancestor.
  // The oldest ancestor (the `http` block) has `depth` zero. Each subsequent
  // generation has the `depth` of its parent plus one.
  int depth;

#ifdef WITH_WAF
  // the thread pool used to run the WAF on
  ngx_thread_pool_t *waf_pool{nullptr};
#endif

#ifdef WITH_RUM
  ngx_flag_t rum_enable = NGX_CONF_UNSET;
  Snippet *rum_snippet = nullptr;
#endif
};

}  // namespace nginx
}  // namespace datadog

#undef DD_NGX_CONF_COMPLEX_UNSET
