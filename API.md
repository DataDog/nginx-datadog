Configuring Datadog Tracing in Nginx
====================================
The behavior of the Datadog nginx module can be customized using dedicated
directives in the nginx configuration.

Nginx configuration files have a hierarchical structure:
```nginx
# "main" context

events {
    # ...
}

http {
    # "http" context

    server {
        # "server" context
        listen 80;

        location /ping {
            # "location" context
            # ...
        }

        location /auth {
            if ($request_method = POST) {
                # "if" context
                # ...
            }
            # ...
        }
    }
}
```
Each context inherits its configuration from the surrounding context.  Contexts
closer to the "main" context are at a "higher level," and contexts more deeply
nested are at a "lower level."

For example, in the configuration above, `location /ping` inherits all of its
configuration from the surrounding `server`, which in turn inherits its
configuration from the surrounding "main" context.

Configuration directives in a given context override part of the configuration
inherited from the higher levels.

Configuration Directives
========================
The Datadog tracing module defines the following configuration directives.

### `datadog`
- **syntax** `datadog { ... }`
- **default**: `{}`
- **context**: `http`, `server`

Configure the Datadog tracer using the specified JSON object.  The object
supports the same properties as those documented in [TODO link to future
dd-opentracing-cpp documentation][1].

The JSON object may include `#`-comments, which are ignored.

### `datadog_enable`
- **syntax** `datadog_enable`
- **context** `http`, `server`, `location`, `if`

Enable Datadog tracing in the current configuration context.  This overrides
any `datadog_disable` directives at higher levels, and may be overriden by
`datadog_disable` directives at lower levels.

When tracing is enabled, a span is created for each request, and trace context
is propagated to the proxied service.

Datadog tracing is enabled by default.

### `datadog_disable`
- **syntax** `datadog_enable`
- **context** `http`, `server`, `location`, `if`

Disable Datadog tracing in the current configuration context.  This overrides
any `datadog_enable` directives at higher levels, and may be overriden by
`datadog_enable` directives at lower levels.

When tracing is disabled, no span is created when a request is handled, and no
trace context is propagated to proxied services.

Datadog tracing is enabled by default.  This directive is the way to disable
it.

### `datadog_trace_locations`

- **syntax** `datadog_trace_locations on|off`
- **default**: `off`
- **context**: `http`, `server`, `location`, `if`

If `on`, then in addition to creating one span per request handled, create an
additional span representing the `location` block selected in handling the
request.

This option is `off` by default, so that only one span is produced per request.

### `datadog_operation_name`

- **syntax** `datadog_operation_name <variable_pattern>`
- **default**: The name of the first location block entered.
- **context**: `http`, `server`, `location`, `if`

Set the request span's "operation name" to the result of evaluating the
specified `<variable_pattern>` in the context of the current request.
`<variable_pattern>` is a string that may contain `$`-[variables][2] (including
those provided by this module).

The request span is the span created while processing a request.

### `datadog_location_operation_name`

- **syntax** `datadog_location_operation_name <variable_pattern>`
- **default**: The name of the location block.
- **context**: `http`, `server`, `location`, `if`

Set the location span's "operation name" to the result of evaluating the
specified `<variable_pattern>` in the context of the current request.
`<variable_pattern>` is a string that may contain `$`-[variables][2] (including
those provided by this module).

The location span is a span created in addition to the request span.  See
`datadog_trace_locations`.

### `datadog_trust_incoming_span`

- **syntax** `datadog_trust_incoming_span on|off`
- **default**: `on`
- **context**: `http`, `server`, `location`, `if`

If `on`, attempt to extract trace context from incoming requests.  This way,
nginx need not be the beginning of the trace — it can inherit a parent span
from the incoming request.

If `off`, trace context will not be extracted from incoming requests.  Nginx
will start a new trace.  This might be desired if extracting trace information
from untrusted clients is deemed a security concern.

### `datadog_tag`

- **syntax** `datadog_tag <key> <variable_pattern>`
- **context**: `http`, `server`, `location`, `if`

On the currently active span, set a tag whose name is the specified `<key>` and
whose value is the result of evaluating the specified `<variable_pattern>` in
the context of the current request.  `<variable_pattern>` is a string that may
contain `$`-[variables][2] (including those provided by this module).

Variables
---------
Nginx defines [variables][2] that may appear in various contexts in the nginx
configuration and at runtime evaluate to values of interest, such as the
request "User-Agent" header (`$http_user_agent`).

The Datadog nginx module defines additional variables that provide information
about the currently active trace.

### `datadog_trace_id`
`$datadog_trace_id` expands to the decimal representation of the unsigned
64-bit ID of the currently active trace.  If there is no currently active
trace, then the variable expands to a hyphen character (`-`) instead.

### `datadog_span_id`
`$datadog_span_id` expands to the decimal representation of the unsigned 64-bit
ID of the currently active span.  If there is no currently active span, then
the variable expands to a hyphen character (`-`) instead.

TODO: request span vs. location span

### `datadog_json`
`$datadog_json` expands to a JSON object of trace context.  Each of its
properties corresponds to the value of a header that would be used to propagate
trace context to a proxied service.

If there is no currently active trace, then the variable expands to a hyphen
character (`-`) instead.

### `datadog_config_json`
`$datadog_config_json` expands to a JSON object whose properties describe the
configuration of the Datadog tracer.  It is not the same as the JSON object
that is used to configure the tracer.  It is the same as the JSON object logged
at "info" level when the tracer is initialized.

If tracing is disabled, then the variable expands to a hyphen character (`-`)
instead.

### `datadog_env_*`
`$datadog_env_<var>` expands to the value of the specified `<var>` environment
variable.  `<var>` must be one of the environment variables used to configure
the Datadog tracer.

If `<var>` is not one of the allowed variables, or if `<var>` is not defined in
the environment, then the variable exapnds to a hyphen character (`-`) instead.

This family of variables is used in the tests for the Datadog nginx module, and
is of little use elsewhere.

### `datadog_propagation_header_*`
`$datadog_propagation_header_<header>` expands to the value of the specified
HTTP `<header>` as it would appear in a request to a service proxied using the
`proxy_pass` directive.

`<header>` is transformed into the name of an HTTP request header by replacing
underscores with hyphens, e.g. `$datadog_propagation_header_x_datadog_origin`
expands to what would be the value of the `X-Datadog-Origin` header were trace
context to be propagated to a service via `proxy_pass`.

This family of variables is used in the implementation of the Datadog nginx
module, and is of little use elsewhere.

[1]: https://TODO
[2]: http://nginx.org/en/docs/varindex.html
