These tests verify that the user can configure the Datadog module using the
`datadog` configuration directive, and that if they don't, a suitable default
configuration is chosen.

There are three classes of configuration scenarios:

1. The user explicitly configured tracing using the `datadog` directive.
2. The user implicitly configured tracing by using a directive that is
   overridden by the Datadog module, such as `proxy_pass`.
3. The user implicitly configured tracing _without_ using any overridden
   directives.  This is probably rare, but it is what would happen if nginx's
   default configuration had the Datadog module added to it (i.e. a static
   content server). 

These tests make sure of the `$datadog_config_json` nginx variable to inspect
the tracer configuration that results from the nginx configuration.
