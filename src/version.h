#pragma once

#include <string_view>

extern "C" {

extern const std::string_view datadog_version_tracer;

extern const std::string_view datadog_semver_nginx_mod;
extern const std::string_view datadog_version_nginx_mod;
extern const std::string_view datadog_build_id_nginx_mod;

extern const std::string_view datadog_semver_nginx;
extern const std::string_view datadog_version_nginx;

#if defined(WITH_WAF)
extern const std::string_view datadog_semver_waf_rules;
extern const std::string_view datadog_version_waf_rules;

extern const std::string_view datadog_semver_libddwaf;
extern const std::string_view datadog_version_libddwaf;
#endif

#if defined(WITH_RUM)
extern const std::string_view datadog_semver_rum_injector;
extern const std::string_view datadog_version_rum_injector;
#endif
}
