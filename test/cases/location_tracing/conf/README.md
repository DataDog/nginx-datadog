By default, an nginx request will produce a single span.

The tracer can be configured to create an additional span for each `location`
entered by nginx as it determines the appropriate upstream.

These tests verify that the `datadog_trace_locations` directive behaves as
expected.
