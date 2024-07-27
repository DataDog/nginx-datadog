#pragma once
// Provides constants for determining the version of the library.
// The strings include more than just the version number to make it easier to
// identify the version of the library in the output of `strings`.

extern "C" {
extern const char *datadog_version_tracer;

extern const char datadog_semver_nginx_mod[];
extern const char datadog_version_nginx_mod[];
extern const char datadog_build_id_nginx_mod[];

extern const char datadog_semver_nginx[];
extern const char datadog_version_nginx[];

#if defined(WITH_WAF)
extern const char datadog_semver_waf_rules[];
extern const char datadog_version_waf_rules[];

extern const char datadog_semver_libddwaf[];
extern const char datadog_version_libddwaf[];
#endif

#if defined(WITH_RUM)
extern const char datadog_semver_rum_injector[];
extern const char datadog_version_rum_injector[];
#endif
}
