These are my notes for things I need to write unit tests for:

- Omitting the `datadog { ... }` directive results in a default config at the first proxy-related directive.
- Using the `opentracing_*` directives prints a warning.
- Using the `datadog { ...}` directive results in an analogous tracer configuration.
- `proxy_pass` forwards tracing context, unless `datadog_disable;`
- `fastcgi_pass` forwards tracing context, unless `datadog_disable;`
- `grpc_pass` forwards tracing context, unless `datadog_disable;`
- Loading the module causes certain `DD_*` environment variables to be
  forwarded to worker processes.
- Default tags are automatically added to traces.
- Location-based tracing is disabled by default.
- `operation_name` is set automatically.
- Logged messages contain trace context.
  - TODO: Is this a good idea?
