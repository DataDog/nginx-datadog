#pragma once

// This component provides a `struct`, `TracingLibrary`, that acts as a
// namespace for behavior specific to the particular tracing implementation.
// This project is based off of `nginx-opentracing`, which dynamically loaded
// an OpenTracing-compatible plugin.  The plugin would provide an `ot::Tracer`
// factory function.  This project no longer loads a plugin, but this component
// maintains some semblance of that interface.  A non-Datadog tracing library
// could in principle be made compatible with this project by changing the
// implementations of `TracingLibrary`'s static member functions.

#include "ot.h"
#include "string_view.h"

#include <opentracing/tracer.h>
#include <opentracing/variant/variant.hpp>

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace datadog {
namespace nginx {

// `NginxVariableFamily` describes a set of nginx configuration variables that
// share a common prefix, and associates with each variable a function that
// fetches a string value for that variable for a specified span.
struct NginxVariableFamily {
    std::string prefix;
    
    using OptionalString = ot::util::variant<std::nullptr_t, string_view>;
    using SpanStringer = std::function<OptionalString(const ot::Span&)>;
    
    // e.g. for the prefix "datadog_", the variable "$datadog_trace_id" is
    // fetched using the function `getter_by_suffix["trace_id"]`.
    std::unordered_map<std::string, SpanStringer> getter_by_suffix;
};

struct TracingLibrary {
    // Return a `Tracer` created with the specified `configuration`. If
    // `configuration` is empty, use a default configuration.  If an error
    // occurs, return `nullptr` and assign a diagnostic to the specified
    // `error`.
    static std::shared_ptr<ot::Tracer> make_tracer(string_view configuration, std::string &error);

    // Parse the specified `configuration` and return the names of span tags
    // used to inject trace context (which tags those are might depend on the
    // configuration, e.g. optional B3 propagation).  If `configuration` is
    // empty, use a default configuration.  If an error occurs, assign a
    // diagnostic to the specified `error`.  Note that the storage to which
    // each returned `string_view` refers must outlive any usage of the
    // return value (realistically this means that they will refer to string
    // literals).
    static std::vector<string_view> span_tag_names(string_view configuration, std::string &error);

    // Parse the specified `configuration` and return a family of nginx
    // variables that will be used to inject propagation headers into proxied
    // requests.  The suffix of each variable name is the name of an injected
    // header, where underscores are first converted into hyphens and all
    // letters are lower-cased.  For example, to inject the header
    // "x-datadog-trace-id", include an entry for "x_datadog_trace_id".  If the
    // prefix were chosen as "datadog_propagation_header_", then the resulting
    // nginx directive for a traced HTTP proxy location would be:
    //
    //     add_header "x-datadog-trace-id" "$datadog_propagation_header_x_datadog_trace_id"
    //
    // If `configuration` is empty, use a default configuration.  If an error
    // occurs, assign a diagnostic message to the specified `error`.
    NginxVariableFamily propagation_headers(string_view configuration, std::string &error);

    // Parse the specified `configuration` and return a family of nginx
    // variables that will be used to fetch string values from the active span.
    // For example, to allow the nginx configuration to access the active
    // span's ID, include an entry for "span_id".  If the prefix were chosen as
    // "datadog_", then the nginx variable "$datadog_span_id" would resolve to
    // whichever value is returned by the corresponding `SpanStringer` when
    // invoked with the active span.
    // If `configuration` is empty, use a default configuration.  If an error
    // occurs, assign a diagnostic message to the specified `error`.
    NginxVariableFamily span_variables(string_view configuration, std::string &error);

    // Return the names of environment variables for worker processes to
    // inherit from the main nginx executable.  Note that the storage to which
    // each returned `string_view` refers must outlive any usage of the
    // return value (realistically this means that they will refer to string
    // literals).
    static std::vector<string_view> environment_variable_names();

    // Return the pattern of an nginx variable script that will be used for the
    // operation name of requests and locations that do not have an operation
    // name defined in the nginx configuration.  Note that the storage to which
    // the returned value refers must outlive any usage of the return value
    // (realistically this means that it will refer to a string literal).
    static string_view default_operation_name_pattern();
    
    // Return a mapping of tag name to nginx variable script pattern.  These
    // tags will be defined automatically during configuration as if they
    // appeared in the nginx configuration file's http section, e.g.
    //
    //     http {
    //       datadog_tag http_user_agent $http_user_agent;
    //       datadog_tag foo bar;
    //       ...
    //     }
    //
    // Note that the storage to which each returned `string_view` refers
    // must outlive any usage of the return value (realistically this means
    // that they will refer to string literals).
    static std::unordered_map<string_view, string_view> default_tags();

    // Return the default setting for whether tracing is enabled in nginx.
    static bool tracing_on_by_default();

    // Return the default setting for whether HTTP locations generate a trace.
    // An HTTP location is an endpoint as configured using a "location" block
    // in the nginx configuration.
    static bool trace_locations_by_default();
};

} // namespace nginx
} // namespace datadog
