These tests verify that the user can configure the Datadog module using the
`datadog_*` configuration directives, and that if they don't, a suitable default
options are chosen.

The following behaviors are tested:

- Certain `datadog_*` directives are allowed as direct children in the `http`
  block only.
- Said directives must appear at most once.
- Omitting said directives results in default values.
- `datadog_propagation_styles`, if present, must precede any `*_pass` directives.

These tests make use of the `$datadog_config_json` nginx variable to inspect
the tracer configuration that results from the nginx configuration.
