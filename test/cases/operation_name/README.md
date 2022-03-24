These tests verify that the "operation name" ("span name") of spans produced
by the module are as configured.

Operation names can be set using the `datadog_operation_name` directive.  If
the directive is not used, then operation name takes on a default value.  See
`TracingLibrary::default_operation_name_pattern()`.

The operation name of request spans and location spans can be set separately.
For location spans, there is the `datadog_location_operation_name` directive.
Both kinds of spans share the same default.
