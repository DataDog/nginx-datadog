These tests verify that the variables `$datadog_trace_id`, `$datadog_span_id`,
and `$datadog_json` are available and produce the expected values in an nginx
configuration.

- `$datadog_trace_id` is the trace ID of the current request.
- `$datadog_span_id` is the span ID of the current request.
- `$datadog_json` is a JSON object containing trace context propagation
  information.  See `void TraceSegment::inject(DictWriter& writer, const SpanData& span)`
  in `dd-trace-cpp/src/datadog/trace_segment.cpp`.
