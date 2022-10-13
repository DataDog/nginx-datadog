These tests verify that the "resource name" ("resource") of spans produced by
the module are as configured.

Resource names can be set using the `datadog_resource_name` directive.  If the
directive is not used, then resource name takes on a default value.  See
`TracingLibrary::default_resource_name_pattern()`.

The resource name of request spans and location spans can be set separately. For
location spans, there is the `datadog_location_resource_name` directive.

These tests closely resemble those in [../operation_name](../operation_name).
