These tests verify that the variables `$datadog_trace_id`, `$datadog_span_id`,
and `$datadog_json` are available and produce the expected values in an nginx
configuration.

- `$datadog_trace_id` is the trace ID of the current request.
- `$datadog_span_id` is the span ID of the current request.
- `$datadog_json` is a JSON object containing trace context propagation
  information.  See `Tracer::Inject(const ot::SpanContext&, std::ostream&)` in
  `dd-opentracing-cpp/src/tracer.cpp`.
