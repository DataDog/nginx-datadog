These are my notes for things I need to write unit tests for:

- ✅ Omitting the `datadog { ... }` directive results in a default config at the first proxy-related directive.
- ✅ Using the `opentracing_*` directives prints a warning.
- ✅ Using the `datadog { ... }` directive results in an analogous tracer configuration.
- ✅ `proxy_pass` forwards tracing context, unless `datadog_disable;`
- ✅ `fastcgi_pass` forwards tracing context, unless `datadog_disable;`
- ✅ `grpc_pass` forwards tracing context, unless `datadog_disable;`
- ✅ Loading the module causes certain `DD_*` environment variables to be
  forwarded to worker processes.
- ✅ Default tags are automatically added to traces.
- ✅ Location-based tracing is disabled by default.
- ✅ `operation_name` is set automatically.
- Logged messages contain trace context.
- The following variables work:
    - `$datadog_trace_id`
    - `$datadog_span_id`
    - `$datadog_json`

Scaffolding
-----------
TODO: A lot of what is written below is no longer true.

Two kinds of tests:
- config test
- behavior test

A config test overwrites the nginx config and then runs `nginx -t` to see if
the config is accepted, and notes any (log) output of the `nginx -t` command.

A behavior test overwrites the nginx config, verifies that it's valid, and then
reloads nginx.  The test can then make one or more requests, and when the
response(s) is (are) received, the test can consume the logs produced by any
of the `docker-compose` services.  Doing so causes the test framework to send
a "sync" request to the corresponding service, so that the "end" of the log
sequence relevant to the test can be determined.

The "sync" request will have to be a different kind of thing for each different
kind of `docker-compose` service:

- http: Bind a port and serve a dedicated `/sync` endpoint there, or honor a
  special `X-Nginx-Datadog-Test-Sync` header in any endpoint.
- fastcgi: Same as above.
- grpc: TODO

There is also a `docker-compose` service, "grpc-client," that converts JSON
HTTP requests to corresponding gRPC requests.   The test driver will use this
to send gRPC requests to nginx, when necessary.  It's a `docker-compose`
service so that dependency management can be handled by Docker, rather than
having a separate "build the test driver" step.

Then `docker-compose` service `nginx` is based on whichever `nginx` image on
Dockerhub corresponds to [nginx-version](nginx-version).

The nginx module must be built before the tests can run.  I'm thinking a
toplevel `make test` could handle that.

```python
def scratch():
    with orchestration.singleton() as orch:
        orch.reset_nginx()
        logs_by_service = orch.sync_proxied_services()

        orch.sync_nginx() # how? Might not need to...
        # Instead, maybe always:
        orch.reset_nginx()

        orch.nginx_request(resource, headers)

        status, log_lines = orch.nginx_test_config(config_text)
        status, log_lines = orch.nginx_set_config(config_text)

class TestThing(unittest.TestCase):
    def test_thing(self):
        """TODO"""
        with orchestration.singleton() as orch:
            config = Path(__file__).with_suffix('.conf').read_text()
            status, log_lines = orch.nginx_test_config(config)
            self.assertEqual(status, 0)
            warning_pattern = r'TODO'
            self.assertTrue(any(re.fullmatch(warning_pattern, line) is not None for line in log_lines))
            
    def test_other_thing(self):
            config = Path(__file__).with_suffix('.conf').read_text()
            status, log_lines = self.orch.nginx_test_config(config)
            self.assertEqual(status, 0)
            warning_pattern = r'TODO'
            self.assertTrue(any(re.fullmatch(warning_pattern, line) is not None for line in log_lines))
```