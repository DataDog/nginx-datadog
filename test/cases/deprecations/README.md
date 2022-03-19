These tests verify that using the deprecated `opentracing_*` configuration
directives produces either a warning or an error, depending on the directive.

The `test_*.py` testing modules use the nginx configuration files under
`conf/`, one for each test method.

See `DEFINE_COMMAND_WITH_OLD_ALIAS` and `plugin_loading_deprecated` in
[ngx_http_datadog_module.cpp][1].

[1]: ../../../src/ngx_http_datadog_module.cpp
