import json
import re

import pytest

from .harness import Workload, trace_id, unique_uri, UNSUPPORTED_IMAGE


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
        diagnostics = "\n".join(
            path.read_text()
            for path in application.directory.glob("nginx-*.log"))
        if failure == "unsupported-version":
            assert "1.20.2" in diagnostics, diagnostics
        assert re.search(
            r"nginx.*(not found|not supported|unsupported|missing|does not exist)|"
            r"(not found|not supported|unsupported|missing|does not exist).*nginx",
            diagnostics, re.IGNORECASE), diagnostics
    finally:
        sandbox.close()
