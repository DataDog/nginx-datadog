import json
import re

import pytest

from .harness import NGINX_VERSION, STABLE_CONFIG_PATHS, Workload, poll, trace_id, unique_uri, UNSUPPORTED_IMAGE

stable_config_xfail = pytest.mark.xfail(
    reason="C++ tracer stable configuration support is not implemented",
    strict=True)
rum_stable_config_xfail = pytest.mark.xfail(
    reason=
    "Nginx RUM does not consume DD_RUM_ENABLED from stable configuration",
    strict=True)
RUM_HTML = "<!doctype html><html><head><title>RUM opt out</title></head><body>tracing only</body></html>"


def nginx_span(sandbox, uri, collector="a", service="injection-nginx"):
    spans = sandbox.wait_spans(uri, service, collector)
    assert len(spans) == 1, spans
    span = spans[0]
    assert span["name"] == "nginx.request", span
    assert int(span["trace_id"]) and int(span["span_id"]), span
    assert span["meta"]["component"] == "nginx"
    assert span["meta"]["http.method"] == "GET"
    assert span["meta"]["http.status_code"] == "200"
    assert span["meta"]["http.url"].endswith(uri)
    return span


def linked_spans(sandbox, uri, expected_trace=None, parent=None):
    nginx = nginx_span(sandbox, uri)
    backend_spans = sandbox.wait_spans(uri, "injection-backend")
    backend = [
        span for span in backend_spans if span["name"] == "flask.request"
    ]
    assert len(backend) == 1, backend_spans
    backend = backend[0]
    assert trace_id(backend) == trace_id(nginx), (nginx, backend)
    assert int(backend["parent_id"]) == int(nginx["span_id"]), (nginx, backend)
    if expected_trace is not None:
        assert trace_id(nginx) == expected_trace
    if parent is not None:
        assert int(nginx["parent_id"]) == parent
    with (sandbox.artifacts / "linked-traces.jsonl").open("a") as evidence:
        evidence.write(
            json.dumps({
                "uri": uri,
                "nginx": nginx,
                "backend": backend
            }) + "\n")
    return nginx, backend


def test_before_and_after_install(sandbox_factory, mode):
    sandbox = sandbox_factory(mode, f"{mode}-before-after")
    try:
        sandbox.start()
        files = ("/workload/app.py", "/workload/nginx.conf")
        before = sandbox.host("sha256sum", *files)
        workload = Workload(sandbox, "before").start()
        uris = [unique_uri(), unique_uri(True)]
        for uri in uris:
            workload.request(uri)
        workload.assert_module(loaded=False)
        workload.finish()
        for uri in uris:
            sandbox.quiet(uri)
        sandbox.install()
        assert sandbox.host("sha256sum", *files) == before
        workload = Workload(sandbox, "after").start()
        for proxy in (False, True):
            uri = unique_uri(proxy)
            workload.request(uri)
            if proxy:
                linked_spans(sandbox, uri)
            else:
                nginx_span(sandbox, uri)
        workload.assert_module()
        workload.finish()
        assert sandbox.host("sha256sum", *files) == before
    finally:
        sandbox.close()


def test_static_request(sandbox, workload):
    application = workload()
    uri = unique_uri()
    assert application.request(uri) == "injection test\n"
    application.assert_module()
    application.stop()
    nginx_span(sandbox, uri)
    sandbox.assert_injection_telemetry()


def test_proxy_trace_link(sandbox, workload):
    application = workload()
    uri = unique_uri(True)
    application.request(
        uri, {
            "x-datadog-trace-id": "123456789",
            "x-datadog-parent-id": "987654321",
            "x-datadog-sampling-priority": "1"
        })
    application.stop()
    linked_spans(sandbox, uri, 123456789, 987654321)


def test_environment_tags(sandbox, workload):
    application = workload({
        "DD_SERVICE": "tagged-nginx",
        "DD_ENV": "injection-test",
        "DD_VERSION": "test-version",
        "DD_TAGS": "team:nginx,test:injection"
    })
    uri = unique_uri()
    application.request(uri)
    application.stop()
    span = nginx_span(sandbox, uri, service="tagged-nginx")
    assert {
        key: span["meta"][key]
        for key in ("env", "version", "team", "test")
    } == {
        "env": "injection-test",
        "version": "test-version",
        "team": "nginx",
        "test": "injection"
    }


@pytest.mark.stable_config
@stable_config_xfail
@pytest.mark.parametrize("path", STABLE_CONFIG_PATHS, ids=("local", "managed"))
def test_stable_config_defaults(sandbox, workload, stable_config, path):
    content = stable_config(
        path, {
            "config_id": "nginx-stable-defaults",
            "apm_configuration_default": {
                "DD_SERVICE": "stable-nginx",
                "DD_ENV": "stable-env",
                "DD_VERSION": "stable-version",
                "DD_TAGS": "team:stable,test:configuration"
            }
        })
    application = workload({"DD_SERVICE": None})
    application.assert_file(path, content)
    uri = unique_uri()
    application.request(uri)
    application.stop()
    span = nginx_span(sandbox, uri, service=None)
    assert span["service"] == "stable-nginx", span
    assert {
        key: span["meta"][key]
        for key in ("env", "version", "team", "test")
    } == {
        "env": "stable-env",
        "version": "stable-version",
        "team": "stable",
        "test": "configuration"
    }


@pytest.mark.stable_config
@stable_config_xfail
def test_stable_config_precedence(sandbox, workload, stable_config):
    local = stable_config(
        STABLE_CONFIG_PATHS[0], {
            "config_id": "nginx-stable-local",
            "apm_configuration_default": {
                "DD_SERVICE": "local-service",
                "DD_VERSION": "local-version"
            }
        })
    managed = stable_config(
        STABLE_CONFIG_PATHS[1], {
            "config_id": "nginx-stable-managed",
            "apm_configuration_default": {
                "DD_SERVICE": "managed-service"
            }
        })
    application = workload({
        "DD_SERVICE": "environment-service",
        "DD_ENV": "environment"
    })
    application.assert_file(STABLE_CONFIG_PATHS[0], local)
    application.assert_file(STABLE_CONFIG_PATHS[1], managed)
    uri = unique_uri()
    application.request(uri)
    application.stop()
    span = nginx_span(sandbox, uri, service=None)
    assert span["service"] == "managed-service", span
    assert span["meta"]["env"] == "environment", span
    assert span["meta"]["version"] == "local-version", span


@pytest.mark.stable_config
@stable_config_xfail
def test_stable_config_targeting_rule(sandbox, workload, stable_config):
    path = STABLE_CONFIG_PATHS[1]
    content = stable_config(
        path, {
            "config_id":
            "nginx-stable-rule",
            "apm_configuration_rules": [{
                "selectors": [{
                    "origin": "environment_variables",
                    "key": "STABLE_CONFIG_SELECTOR",
                    "operator": "equals",
                    "matches": ["true"]
                }],
                "configuration": {
                    "DD_SERVICE": "targeted-service"
                }
            }]
        })
    application = workload({
        "STABLE_CONFIG_SELECTOR": "true",
        "DD_SERVICE": "environment-service"
    })
    application.assert_file(path, content)
    uri = unique_uri()
    application.request(uri)
    application.stop()
    span = nginx_span(sandbox, uri, service=None)
    assert span["service"] == "targeted-service", span


@pytest.mark.stable_config
@stable_config_xfail
@pytest.mark.parametrize("path", STABLE_CONFIG_PATHS, ids=("local", "managed"))
def test_stable_config_tracing_disabled(sandbox, workload, stable_config,
                                        path):
    content = stable_config(
        path, {
            "config_id": "nginx-stable-disabled",
            "apm_configuration_default": {
                "DD_TRACE_ENABLED": False
            }
        })
    application = workload()
    application.assert_file(path, content)
    uri = unique_uri()
    application.request(uri)
    application.assert_module()
    application.stop()
    sandbox.quiet(uri, "injection-nginx")


@pytest.mark.stable_config
def test_rum_disabled_by_environment(sandbox, workload, stable_config):
    path = STABLE_CONFIG_PATHS[0]
    content = stable_config(
        path, {
            "config_id": "nginx-rum-environment-opt-out",
            "apm_configuration_default": {
                "DD_RUM_ENABLED": True,
                "DD_RUM_APPLICATION_ID": "stable-application",
                "DD_RUM_CLIENT_TOKEN": "stable-token"
            }
        })
    application = workload({"DD_RUM_ENABLED": "false"})
    application.assert_file(path, content)
    uri = f"{unique_uri()}.html"
    body = application.request(uri, content=RUM_HTML)
    application.assert_module()
    application.stop()
    nginx_span(sandbox, uri)
    assert body == RUM_HTML
    assert "datadog-rum.js" not in body


@pytest.mark.stable_config
@rum_stable_config_xfail
@pytest.mark.parametrize("path", STABLE_CONFIG_PATHS, ids=("local", "managed"))
def test_rum_disabled_by_stable_config(sandbox, workload, stable_config, path):
    content = stable_config(
        path, {
            "config_id": "nginx-rum-stable-opt-out",
            "apm_configuration_default": {
                "DD_RUM_ENABLED": False,
                "DD_RUM_APPLICATION_ID": "stable-application",
                "DD_RUM_CLIENT_TOKEN": "stable-token"
            }
        })
    application = workload()
    application.assert_file(path, content)
    uri = f"{unique_uri()}.html"
    body = application.request(uri, content=RUM_HTML)
    application.assert_module()
    application.stop()
    nginx_span(sandbox, uri)
    assert body == RUM_HTML
    assert "datadog-rum.js" not in body


@pytest.mark.parametrize("routing", ["url", "host-port"])
def test_agent_routing(sandbox, workload, routing):
    environment = {
        "DD_TRACE_AGENT_URL": "http://127.0.0.1:9126"
    } if routing == "url" else {
        "DD_TRACE_AGENT_URL": None,
        "DD_AGENT_HOST": "127.0.0.1",
        "DD_TRACE_AGENT_PORT": "9126"
    }
    application = workload(environment)
    uri = unique_uri()
    application.request(uri)
    application.stop()
    nginx_span(sandbox, uri, "b")


def test_agent_url_precedence(sandbox, workload):
    application = workload({
        "DD_TRACE_AGENT_URL": "http://127.0.0.1:8126",
        "DD_AGENT_HOST": "127.0.0.1",
        "DD_TRACE_AGENT_PORT": "9126"
    })
    uri = unique_uri()
    application.request(uri)
    application.stop()
    nginx_span(sandbox, uri)
    sandbox.quiet(uri, "injection-nginx", collector="b")


def test_agent_unix_socket(sandbox, workload):
    environment = {"DD_TRACE_AGENT_URL": "unix:///var/run/datadog/apm.socket"}
    application = workload(environment, environment)
    uri = unique_uri(True)
    application.request(uri)
    application.stop()
    linked_spans(sandbox, uri)


def test_w3c_propagation(sandbox, workload):
    environment = {
        "DD_TRACE_PROPAGATION_STYLE_EXTRACT": "tracecontext",
        "DD_TRACE_PROPAGATION_STYLE_INJECT": "tracecontext"
    }
    application = workload(environment, environment)
    uri = unique_uri(True)
    incoming = "1234567890abcdef1234567890abcdef"
    parent = "1234567890abcdef"
    response = json.loads(
        application.request(uri,
                            {"traceparent": f"00-{incoming}-{parent}-01"}))
    application.stop()
    nginx, _ = linked_spans(sandbox, uri, int(incoming, 16), int(parent, 16))
    assert response["headers"][
        "traceparent"] == f"00-{incoming}-{int(nginx['span_id']):016x}-01"


def test_sampling_rule(sandbox, workload):
    uri = unique_uri(True)
    rules = [{
        "sample_rate": 0,
        "service": "injection-nginx",
        "resource": f"GET {uri}"
    }]
    application = workload({
        "DD_TRACE_SAMPLING_RULES": json.dumps(rules),
        "DD_TRACE_PROPAGATION_STYLE_INJECT": "datadog"
    })
    response = json.loads(application.request(uri))
    assert response["headers"]["x-datadog-sampling-priority"] == "-1", response


def assert_disabled(sandbox, workload, disabled):
    variable = "DD_TRACE_ENABLED" if disabled == "reporting" else "DD_INSTRUMENT_SERVICE_WITH_APM"
    application = workload({variable: "false"})
    uri = unique_uri(True)
    application.request(uri)
    application.assert_module(loaded=disabled == "reporting")
    control = unique_uri(True)
    application.request(control, port=8081)
    sandbox.wait_spans(control, "injection-backend")
    application.stop()
    sandbox.quiet(uri, "injection-nginx")


def test_reporting_disabled(sandbox, workload):
    assert_disabled(sandbox, workload, "reporting")


def test_injection_opt_out(sandbox, workload):
    assert_disabled(sandbox, workload, "injection")


def test_reload_and_restart(sandbox, workload):
    application = workload({
        "DD_ENV": "restart-test",
        "DD_VERSION": "restart-version"
    })
    uris = []
    for action in (lambda: None, application.reload, lambda:
                   (application.stop(), application.start())):
        action()
        application.assert_module()
        uri = unique_uri()
        uris.append(uri)
        application.request(uri)
    application.stop()
    for uri in uris:
        span = nginx_span(sandbox, uri)
        assert span["meta"]["env"] == "restart-test"
        assert span["meta"]["version"] == "restart-version"


@pytest.mark.parametrize("failure", ["missing-package", "unsupported-version"])
def test_unavailable_module(sandbox_factory, mode, failure):
    sandbox = sandbox_factory(mode, f"{mode}-{failure}")
    try:
        sandbox.start()
        sandbox.install(nginx=failure != "missing-package")
        image = None
        if failure == "unsupported-version":
            image = UNSUPPORTED_IMAGE + ("-alpine"
                                         if mode == "docker-alpine" else "")
            sandbox.images.pull(image)
            if mode == "host":
                sandbox.load(image)
                sandbox.inner("create", "--runtime=runc", "--name",
                              "old-nginx", image)
                sandbox.inner("cp", "old-nginx:/usr/sbin/nginx",
                              "/tmp/unsupported-nginx")
                dependencies = sandbox.inner("run", "--rm", "--runtime=runc",
                                             image, "ldd", "/usr/sbin/nginx")
                for library in re.findall(r"=> (/\S+)", dependencies):
                    if sandbox.host("test", "-e", library,
                                    check=False).returncode:
                        sandbox.inner("cp", "-L", f"old-nginx:{library}",
                                      library)
                sandbox.host("cp", "/tmp/unsupported-nginx", "/usr/sbin/nginx")
        application = Workload(sandbox, failure, image=image).start()
        uri = unique_uri(True)
        application.request(uri)
        application.assert_module(loaded=False)
        sandbox.wait_spans(uri, "injection-backend")
        application.finish()
        sandbox.quiet(uri, "injection-nginx")
        diagnostics = poll(
            lambda: [
                event for event in sandbox.agent("/test/apmtelemetry")
                if event.get("request_type") == "injection-metadata" and event.
                get("application", {}).get("language_name") == "nginx"
            ], "Nginx skipped-injection diagnostic")
        version = "1.20.2" if failure == "unsupported-version" else NGINX_VERSION
        for event in diagnostics:
            assert event["application"]["language_version"] == version, event
            assert event["payload"]["result"] == "abort", event
            assert event["payload"]["result_reason"] == \
                "the tracing library is not installed for the language", event
    finally:
        sandbox.close()
