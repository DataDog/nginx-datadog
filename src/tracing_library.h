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

#include <opentracing/string_view.h>
#include <opentracing/tracer.h>

#include <memory>
#include <string>
#include <vector>

namespace datadog {
namespace nginx {

struct TracingLibrary {
    // Return a `Tracer` created with the specified `configuration`. If
    // `configuration` is empty, use a default configuration.  If an error
    // occurs, return `nullptr` and assign a diagnostic to the specified
    // `error`.
    static std::shared_ptr<ot::Tracer> make_tracer(ot::string_view configuration, std::string &error);

    // Parse the specified `configuration` and return the names of span tags
    // used to inject trace context (which tags those are might depend on the
    // configuration, e.g. optional B3 propagation).  If `configuration` is
    // empty, use a default configuration.  If an error occurs, assign a
    // diagnostic to the specified `error`.  Note that the storage to which
    // each returned `ot::string_view` refers must outlive any usage of the
    // return value (realistically this means that they will refer to string
    // literals).
    static std::vector<ot::string_view> span_tag_names(ot::string_view configuration, std::string &error);

    // Return the names of environment variables for worker processes to
    // inherit from the main nginx executable.  Note that the storage to which
    // each returned `ot::string_view` refers must outlive any usage of the
    // return value (realistically this means that they will refer to string
    // literals).
    static std::vector<ot::string_view> environment_variable_names();

    // Return the pattern of an nginx variable script that will be used for the
    // operation name of requests and locations that do not have an operation
    // name defined in the nginx configuration.  Note that the storage to which
    // the returned value refers must outlive any usage of the return value
    // (realistically this means that it will refer to a string literal).
    static ot::string_view default_operation_name_pattern();

    // Return the default setting for whether tracing is enabled in nginx.
    static bool tracing_on_by_default();

    // Return the default setting for whether HTTP locations generate a trace.
    // An HTTP location is an endpoint as configured using a "location" block
    // in the nginx configuration.
    static bool trace_locations_by_default();

    // Return whether to allow the tracer JSON configuration inline within
    // the nginx configuration using the `opentracing_configure` directive.
    // If `false`, then the `opentracing_configuration_file` directive must be
    // used instead.
    static bool configure_tracer_json_inline();
};

} // namespace nginx
} // namespace datadog
