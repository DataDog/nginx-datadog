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

### `datadog_agent_url`
- **syntax** `datadog_agent_url <url>`
- **default**: `http://localhost:8126`
- **context**: `http`

Specify a URL at which the Datadog Agent can be contacted.
The following formats are supported:

- `http://<domain or IP>:<port>`
- `http://<domain or IP>`
- `http+unix://<path to socket>`
- `unix://<path to socket>`

The port defaults to 8126 if it is not specified.

### `datadog_service_name`
- **syntax** `datadog_service_name <name>`
- **default**: `nginx`
- **context**: `http`

Set the service name to associate with each span produced by this module.

### `datadog_service_type`
- **syntax** `datadog_service_type <type>`
- **default**: `web`
- **context**: `http`

Set the service type to associate with each span produced by this module.

See [Datadog's documentation][3] for a description of service type.

### `datadog_environment`
- **syntax** `datadog_environment <environment>`
- **default**: (no value)
- **context**: `http`

Set the name of the environment within which nginx is running. Common values
include `prod`, `dev`, and `staging`.

### `datadog_propagation_styles`
- **syntax** `datadog_propagation_styles <style> [<style> ...]`
- **default**: `tracecontext datadog`
- **context**: `http`

Set one or more trace propagation styles that nginx will use to extract trace
context from incoming requests and to inject trace context into outgoing
requests.

When extracting trace context from an incoming request, the specified styles
will be tried in order, stopping at the first style that yields trace context.

When injecting trace context into an outgoing request, all of the specified
styles will be used.

The following styles are supported:

- `datadog` is the Datadog style.  It uses the following headers:
    - X-Datadog-Trace-Id
    - X-Datadog-Parent-Id
    - X-Datadog-Sampling-Priority
    - X-Datadog-Origin
    - X-Datadog-Tags
- `tracecontext` is the W3C (OpenTelemetry) style.  It uses the following headers:
    - traceparent
    - tracestate
- `b3` is the Zipkin multi-header style.  It uses the following headers:
    - X-B3-TraceId
    - X-B3-SpanId
    - X-B3-Sampled

### `datadog_sample_rate`
- **syntax** `datadog_sample_rate <rate> [on|off]`
- **default**: `tracecontext datadog`
- **context**: `http`, `server`, `location`

Set the probability that the traces beginning with requests in this
configuration context will be kept (sent to Datadog), as opposed to dropped.

The `<rate>` is a number between 0.0 and 1.0, inclusive. Zero indicates "never,"
while one indicates "always." `0.5` would indicate "half the time," i.e. 50%.

Optionally specify a third argument that is a variable expression that must
evaluate to either `on` or `off`.  If it evaluates to `on`, then the associated
sample rate is chosen.  If it evaluates to `off`, then the associated sample
rate is ignored. Any other value behaves as `off` and logs an error. If the
third argument is omitted, then it defaults to `on`.

`datadog_sample_rate` directives in lower level configuration contexts take
precedence over those in higher level configuration contexts. If multiple
directives appear at the same configuration level, then their `on`/`off`
expressions are tried in order.  The first directive whose expression evaluates
to `on` is the rate applied at that configuration level.

For example, consider the following excerpt from an nginx configuration file:
```nginx
 1  http {
 2      map $http_x_request_category $healthcheck_toggle {
 3          healthcheck    on;
 4          default    off;
 5      }
 6
 7      datadog_sample_rate 0 $healthcheck_toggle;
 8      datadog_sample_rate 0.1;
 9
10      server {
11          listen 80;
12
13          location / {
14              proxy_pass http://upstream;
15          }
16
17          location /admin {
18              datadog_sample_rate 1.0;
19              proxy_pass http://admin-portal;
20          }
21      }
22  }
```

The `location /` at line 13 does not have its own `datadog_sample_rate`, so it
inherits any from enclosing configuration contexts.  There are no
`datadog_sample_rate` directives directly in the enclosing `server` block, but
there are in the enclosing `http` block. The first is on line 7, and is
conditional based on the value of the `$healthceck_toggle` variable, which is
defined by the `map` on line 2.

A request that routes to `location /` will be sampled at 0% if the
`X-Request-Category` header has the value `healthcheck`, per the
`datadog_sample_rate` on line 7. If the `X-Request-Category` header is no
present or has a value different from `healthcheck`, then the request will be
sampled at 10%, per the `datadog_sample_rate` on line 8.

A request that routes to `location /admin` will be sampled at 100%, per the
`datadog_sample_rate` on line 19.

The `datadog_sample_rate` directive that applies, if any, is annotated in the
request span as the `nginx.sample_rate_source` tag. The tag has the following
format:
```
<nginx file path>:<line>#<dupe>
```
For example,
```
/etc/nginx/nginx.conf:23#1
```
- `<nginx file path>` is the path to the nginx configuration file that contains
  the `datadog_sample_rate` directive.
- `<line>` is the line number where the directive appears.
- `<dupe>` is the one-based index of the directive among all
  `datadog_sample_rate` directives that appear on that same line. Typically each
  directive is on its own line, so `<dupe>` is likely always `1`.

### `datadog_enable`
- **syntax** `datadog_enable`
- **context** `http`, `server`, `location`

Enable Datadog tracing in the current configuration context.  This overrides
any `datadog_disable` directives at higher levels, and may be overridden by
`datadog_disable` directives at lower levels.

When tracing is enabled, a span is created for each request, and trace context
is propagated to the proxied service.

Datadog tracing is enabled by default.

### `datadog_disable`
- **syntax** `datadog_enable`
- **context** `http`, `server`, `location`

Disable Datadog tracing in the current configuration context.  This overrides
any `datadog_enable` directives at higher levels, and may be overridden by
`datadog_enable` directives at lower levels.

When tracing is disabled, no span is created when a request is handled, and no
trace context is propagated to proxied services.

Datadog tracing is enabled by default.  This directive is the way to disable
it.

### `datadog_trace_locations`

- **syntax** `datadog_trace_locations on|off`
- **default**: `off`
- **context**: `http`, `server`, `location`

If `on`, then in addition to creating one span per request handled, create an
additional span representing the `location` block selected in handling the
request.

This option is `off` by default, so that only one span is produced per request.

### `datadog_operation_name`

- **syntax** `datadog_operation_name <variable_pattern>`
- **default**: The name of the first location block entered.
- **context**: `http`, `server`, `location`

Set the request span's "operation name" to the result of evaluating the
specified `<variable_pattern>` in the context of the current request.
`<variable_pattern>` is a string that may contain `$`-[variables][2] (including
those provided by this module).

The request span is the span created while processing a request.

### `datadog_location_operation_name`

- **syntax** `datadog_location_operation_name <variable_pattern>`
- **default**: The name of the location block.
- **context**: `http`, `server`, `location`

Set the location span's "operation name" to the result of evaluating the
specified `<variable_pattern>` in the context of the current request.
`<variable_pattern>` is a string that may contain `$`-[variables][2] (including
those provided by this module).

The location span is a span created in addition to the request span.  See
`datadog_trace_locations`.

### `datadog_resource_name`

- **syntax** `datadog_resource_name <variable_pattern>`
- **default**: `$request_method $uri`, e.g. "GET /api/book/0-345-24223-8/title"
- **context**: `http`, `server`, `location`

Set the request span's "resource name" (sometimes called "endpoint") to the
result of evaluating the specified `<variable_pattern>` in the context of the
current request. `<variable_pattern>` is a string that may contain
`$`-[variables][2] (including those provided by this module).

The request span is the span created while processing a request.

### `datadog_location_resource_name`

- **syntax** `datadog_location_resource_name <variable_pattern>`
- **default**: `$request_method $uri`, e.g. "GET /api/book/0-345-24223-8/title"
- **context**: `http`, `server`, `location`

Set the location span's "resource name" (sometimes called "endpoint") to the
result of evaluating the specified `<variable_pattern>` in the context of the
current request. `<variable_pattern>` is a string that may contain
`$`-[variables][2] (including those provided by this module).

The location span is a span created in addition to the request span.  See
`datadog_trace_locations`.

### `datadog_trust_incoming_span`

- **syntax** `datadog_trust_incoming_span on|off`
- **default**: `on`
- **context**: `http`, `server`, `location`

If `on`, attempt to extract trace context from incoming requests.  This way,
nginx need not be the beginning of the trace — it can inherit a parent span
from the incoming request.

If `off`, trace context will not be extracted from incoming requests.  Nginx
will start a new trace.  This might be desired if extracting trace information
from untrusted clients is deemed a security concern.

### `datadog_tag`

- **syntax** `datadog_tag <key> <variable_pattern>`
- **context**: `http`, `server`, `location`

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

Note that if `datadog_trace_locations` is `on`, then `$datadog_span_id` will
refer to the span associated with the location (outbound request), not its
parent (inbound request).

### `datadog_location`
`$datadog_location` expands to the name or pattern of the `location` block that
matched the current request.  For example,

    location /foo {
        # ...
    }

has location name "/foo", while

    location ~ /api/v(1|2)/trace/[0-9]+ {
        # ...
    }

has location name "`/api/v(1|2)/trace/[0-9]+`".

Named locations have their literal names, including the "@", e.g.

    location @updates {
        # ...
    }

has location name "@updates".

If there is no location associated with the current request, then
`$datadog_location` expands to a hyphen character ("-").

### `datadog_proxy_directive`
`$datadog_proxy_directive` expands to the name of the configuration directive
used to proxy the current request, i.e. one of `proxy_pass`, `grpc_pass`, or
`fastcgi_pass`.

If the request was not configured by one of those directives, then
`$datadog_proxy_directive` expands to "`location`".

This variable is used in the implementation of the Datadog nginx module.

### `datadog_json`
`$datadog_json` expands to a JSON object of trace context.  Each of its
properties corresponds to the value of a header that would be used to propagate
trace context to a proxied service.

If there is no currently active trace, then the variable expands to a hyphen
character (`-`) instead.

### `datadog_config_json`
`$datadog_config_json` expands to a JSON object whose properties describe the
configuration of the Datadog tracer.  It is the same as the JSON object logged
to standard error when the tracer is initialized.

If tracing is disabled, then the variable expands to a hyphen character (`-`)
instead.

### `datadog_env_*`
`$datadog_env_<var>` expands to the value of the specified `<var>` environment
variable.  `<var>` must be one of the environment variables used to configure
the Datadog tracer.

If `<var>` is not one of the allowed variables, or if `<var>` is not defined in
the environment, then the variable expands to a hyphen character (`-`) instead.

This family of variables is used in the tests for the Datadog nginx module.

### `datadog_propagation_header_*`
`$datadog_propagation_header_<header>` expands to the value of the specified
HTTP `<header>` as it would appear in a request to a service proxied using the
`proxy_pass` directive.

`<header>` is transformed into the name of an HTTP request header by replacing
underscores with hyphens, e.g. `$datadog_propagation_header_x_datadog_origin`
expands to what would be the value of the `X-Datadog-Origin` header were trace
context to be propagated to a service via `proxy_pass`.

This family of variables is used in the implementation of the Datadog nginx
module.

[1]: https://github.com/DataDog/dd-opentracing-cpp/blob/master/doc/configuration.md
[2]: http://nginx.org/en/docs/varindex.html
[3]: https://docs.datadoghq.com/tracing/services/services_list/#services-types
