from .. import case
from .. import formats

import os
import json
from pathlib import Path


def find_mismatches(pattern, subject):
    """Return a list of all of the ways that `subject` differs from `pattern`.

    `pattern` is a composition of `dict` and `list`, where each `dict` asserts
    keys that the corresponding `dict` within `subject` must have, and the value
    at each key must match the corresponding pattern in `pattern`.

    Corresponding lists in `pattern` and `subject` must have the same length,
    and the elements in `subject` must match the corresponding patterns in
    `pattern`.

    For example, the following invocation

        find_mismatches(
            pattern={'foo': [1, 2], 'bar': {'baz': 3}},
            subject={'foo': [1, 2], 'boo': 4, 'bar': {'bax': 3}})

    would return a list containing the one mismatch between `pattern` and
    `subject`:

        [{'path': '.bar', 'error': 'missing key', 'key': 'baz', 'actual': {'bax': 3}}]

    because the value at the subject's "bar" key does not have a "baz" key.

    If there are no mismatches, then return an empty `list`.
    """

    def yield_mismatches(path, pattern, subject):
        if type(pattern) is not type(subject):
            yield {
                "path": path,
                "error": "mismatched types",
                "pattern": pattern,
                "actual": subject,
            }
        elif isinstance(pattern, list):
            if len(pattern) != len(subject):
                yield {
                    "path": path,
                    "error": "mismatched list lengths",
                    "pattern": pattern,
                    "actual": subject,
                }
                return
            for i in range(len(pattern)):
                yield from yield_mismatches(path + f".{i}", pattern[i],
                                            subject[i])
        elif isinstance(pattern, dict):
            for key, subpattern in pattern.items():
                if key not in subject:
                    yield {
                        "path": path,
                        "error": "missing key",
                        "key": key,
                        "actual": subject,
                    }
                else:
                    yield from yield_mismatches(f"{path}.{key}", subpattern,
                                                subject[key])
        elif pattern != subject:
            yield {
                "path": path,
                "error": "mismatched values",
                "expected": pattern,
                "actual": subject,
            }

    return list(yield_mismatches("", pattern, subject))


def is_tracing_header(x: str) -> bool:
    return "datadog" in x or "trace" in x


class TestOTelDropInSupport(case.TestCase):

    def test_variables(self):
        conf_path = Path(__file__).parent / "./conf/otel_variables.conf"
        conf_text = conf_path.read_text()

        status, log_lines = self.orch.nginx_replace_config(
            conf_text, conf_path.name)
        self.assertEqual(0, status, log_lines)

        status, _, body = self.orch.send_nginx_http_request("/http")
        self.assertEqual(200, status)
        response = json.loads(body)
        headers = response["headers"]
        traceparent = headers["traceparent"]
        assert traceparent
        _, trace_id, span_id, _ = traceparent.split("-")

        # The service being reverse proxied by nginx returns a JSON response
        # containing the request headers.  By instructing nginx to add headers
        # whose values depend on the variables, we can extract the values of
        # the variables from the response.
        self.assertIn("x-datadog-test-thingy", headers)
        header_trace_id, header_span_id = json.loads(
            headers["x-datadog-test-thingy"])
        self.assertEqual(trace_id, header_trace_id)
        self.assertEqual(span_id, header_span_id)

    def test_directives(self):
        conf_path = Path(__file__).parent / "./conf/otel_directives.conf"
        conf_text = conf_path.read_text()

        status, log_lines = self.orch.nginx_replace_config(
            conf_text, conf_path.name)
        self.assertEqual(0, status, log_lines)

        status, _, body = self.orch.send_nginx_http_request("/config")
        self.assertEqual(200, status)

        config = json.loads(body)
        rules = [{
            "name": "*",
            "resource": "*",
            "sample_rate": 1.0,
            "service": "*",
            "tags": {
                "nginx.sample_rate_source": "/datadog-tests/nginx.conf:20#1"
            },
        }]
        if os.getenv("NGINX_FLAVOR", "") == "ingress-nginx":
            rules.append({
                "name": "*",
                "resource": "GET /is-dynamic-lb-initialized",
                "sample_rate": 0.0,
                "service": "*",
                "tags": {},
            })
            rules.append({
                "name": "*",
                "resource": "GET /nginx_status",
                "sample_rate": 0.0,
                "service": "*",
                "tags": {},
            })

        pattern = {
            "service": "foo",
            "collector": {
                "config": {
                    "traces_url": "http://my_agent:8015/v0.4/traces"
                }
            },
            "report_traces": True,
            "trace_sampler": {
                "rules": rules
            },
        }
        mismatches = find_mismatches(pattern, config)
        self.assertEqual(mismatches, [])

        # Consume any previous logs
        self.orch.sync_service("agent")

        status, _, body = self.orch.send_nginx_http_request("/http")
        self.assertEqual(200, status)

        # Force flushing
        self.orch.reload_nginx()
        log_lines = self.orch.sync_service("agent")

        response = json.loads(body)
        tracing_context = [
            x for x in response["headers"] if is_tracing_header(x)
        ]
        self.assertTrue(len(tracing_context) > 0)

        for line in log_lines:
            segments = formats.parse_trace(line)
            if segments is None:
                continue

            for segment in segments:
                for span in segment:
                    if span["service"] != "nginx":
                        continue

                    tags = span["meta"]
                    my_otel_tag = tags.get("my_otel_tag", "")
                    self.assertEqual("otelotel", my_otel_tag)
                    break

    def test_opentelemetry_off(self):
        conf_path = Path(__file__).parent / "./conf/otel_directives.conf"
        conf_text = conf_path.read_text()

        status, log_lines = self.orch.nginx_replace_config(
            conf_text, conf_path.name)
        self.assertEqual(0, status, log_lines)

        status, _, body = self.orch.send_nginx_http_request("/disable-tracing")
        self.assertEqual(200, status)

        response = json.loads(body)
        self.assertEqual(response["service"], "http")

        tracing_context = [
            x for x in response["headers"] if is_tracing_header(x)
        ]
        self.assertEqual(0, len(tracing_context))
