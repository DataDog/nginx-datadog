from .. import formats
from .. import case
from pathlib import Path
import json


class TestApmTracingEnabled(case.TestCase):
    requires_waf = True
    config_setup_done = False

    # Constants for distributed tracing tests
    DISTRIBUTED_TRACE_ID_DEC = 7337010839542040699
    DISTRIBUTED_PARENT_ID_DEC = 2356450358339785194
    INJECTED_SAMPLING_PRIORITY_USER_DROP = -1
    INJECTED_SAMPLING_PRIORITY_AUTO_KEEP = 1
    INJECTED_SAMPLING_PRIORITY_USER_KEEP = 2
    X_DATADOG_ORIGIN_RUM = "rum"

    def setUp(self):
        super().setUp()
        # avoid reconfiguration (cuts time almost in half)
        if not TestApmTracingEnabled.config_setup_done:
            waf_path = Path(__file__).parent / "./conf/waf.json"
            waf_text = waf_path.read_text()
            self.orch.nginx_replace_file("/tmp/waf.json", waf_text)

            conf_path = Path(__file__).parent / "conf" / "apm_tracing_off.conf"
            conf_text = conf_path.read_text()

            status, log_lines = self.orch.nginx_replace_config(
                conf_text, conf_path.name)
            self.assertEqual(0, status, log_lines)

            # clear any previous agent data
            self.orch.sync_service("agent")

            TestApmTracingEnabled.config_setup_done = True

    def get_traces(self, on_chunk):
        self.orch.reload_nginx()
        log_lines = self.orch.sync_service("agent")
        # Find the trace that came from nginx, and pass its chunks (groups of
        # spans) to the callback.
        found_nginx_trace = False
        for line in log_lines:
            trace = formats.parse_trace(line)
            if trace is None:
                continue
            for chunk in trace:
                if chunk[0]["service"] != "nginx":
                    continue
                found_nginx_trace = True
                on_chunk(chunk)

        return found_nginx_trace

    def test_apm_tracing_off_no_waf(self):
        for _ in range(2):
            status, _, body = self.orch.send_nginx_http_request("/")
            self.assertEqual(200, status)
            self.assertEqual(body, "apm_tracing_off")

        is_first = True

        def on_chunk(chunk):
            nonlocal is_first
            self.assertEqual(len(chunk), 1, "Expected one span in the trace")
            span = chunk[0]
            self.assertNotIn("_dd.p.ts", span["meta"])
            self.assertNotIn("_dd.p.dm", span["metrics"])

            if is_first:
                self.assertEqual(span["metrics"]["_dd.apm.enabled"], 0)
                self.assertEqual(span["metrics"]["_sampling_priority_v1"],
                                 2)  # USER_KEEP
                is_first = False
            else:
                self.assertEqual(span["metrics"]["_dd.apm.enabled"], 0)
                self.assertEqual(span["metrics"]["_sampling_priority_v1"],
                                 -1)  # drop

        self.assertTrue(self.get_traces(on_chunk))

    def test_apm_tracing_off_waf(self):
        for _ in range(2):
            status, _, body = self.orch.send_nginx_http_request(
                "/http/", headers={"User-agent": "no_block"})
            self.assertEqual(200, status)

            response = json.loads(body)
            forwarded_headers = response["headers"]

            self.assertEqual(forwarded_headers["x-datadog-sampling-priority"],
                             "2")

            self.assertIn("x-datadog-tags", forwarded_headers,
                          "Missing x-datadog-tags header")
            tags = forwarded_headers["x-datadog-tags"]
            self.assertIn("_dd.p.ts=02", tags,
                          f"Missing _dd.p.ts=02 in x-datadog-tags: {tags}")
            self.assertIn("_dd.p.dm=-5", tags,
                          f"Missing _dd.p.dm=-5 in x-datadog-tags: {tags}")

        def on_chunk(chunk):
            self.assertEqual(len(chunk), 1, "Expected one span in the trace")
            span = chunk[0]

            self.assertEqual(span["metrics"]["_dd.apm.enabled"], 0)
            self.assertEqual(span["metrics"]["_sampling_priority_v1"],
                             2)  # USER_KEEP
            self.assertEqual(span["meta"]["_dd.p.ts"], "02")
            self.assertEqual(span["meta"]["_dd.p.dm"], "-4")

        self.assertTrue(self.get_traces(on_chunk))

    def test_distributed_tracing_no_waf(self):
        headers = {
            "x-datadog-trace-id":
            str(self.DISTRIBUTED_TRACE_ID_DEC),
            "x-datadog-parent-id":
            str(self.DISTRIBUTED_PARENT_ID_DEC),
            "x-datadog-sampling-priority":
            str(self.INJECTED_SAMPLING_PRIORITY_USER_KEEP),
            "x-datadog-origin":
            self.X_DATADOG_ORIGIN_RUM,
        }
        request_count = 0
        for _ in range(2):
            request_count += 1
            status, _, body = self.orch.send_nginx_http_request(
                "/http", headers=headers)
            self.assertEqual(200, status)

            # For the first request, assert the propagation header values
            if request_count == 1:
                response = json.loads(body)
                forwarded_headers = response["headers"]

                # Assert that the propagation headers match the expected format
                # The trace ID should remain the same, but parent ID should be different (new span)
                self.assertIn(
                    "x-datadog-trace-id",
                    forwarded_headers,
                    "Missing x-datadog-trace-id header",
                )
                self.assertEqual(
                    forwarded_headers["x-datadog-trace-id"],
                    str(self.DISTRIBUTED_TRACE_ID_DEC),
                    f"x-datadog-trace-id mismatch: expected {self.DISTRIBUTED_TRACE_ID_DEC}, got {forwarded_headers['x-datadog-trace-id']}",
                )

                self.assertIn(
                    "x-datadog-parent-id",
                    forwarded_headers,
                    "Missing x-datadog-parent-id header",
                )
                self.assertNotEqual(
                    forwarded_headers["x-datadog-parent-id"],
                    str(self.DISTRIBUTED_PARENT_ID_DEC),
                    f"x-datadog-parent-id should be different from original {self.DISTRIBUTED_PARENT_ID_DEC}, but got {forwarded_headers['x-datadog-parent-id']}",
                )

                expected_headers = {
                    "x-datadog-sampling-priority":
                    str(self.INJECTED_SAMPLING_PRIORITY_USER_KEEP),
                    "x-datadog-origin":
                    self.X_DATADOG_ORIGIN_RUM,
                }

                for header_name, expected_value in expected_headers.items():
                    self.assertIn(header_name, forwarded_headers)
                    self.assertEqual(forwarded_headers[header_name],
                                     expected_value)

            # For the second request, assert that there are no propagation headers
            if request_count == 2:
                # Parse the response to check that no propagation headers are present
                response = json.loads(body)
                forwarded_headers = response.get("headers", {})

                # Assert that propagation headers are NOT present on the second request
                propagation_headers = [
                    "x-datadog-trace-id",
                    "x-datadog-parent-id",
                    "x-datadog-sampling-priority",
                    "x-datadog-origin",
                    "x-datadog-tags",
                    "traceparent",
                    "tracestate",
                ]

                for header_name in propagation_headers:
                    self.assertNotIn(header_name, forwarded_headers)

        span_count = 0

        def on_chunk(chunk):
            nonlocal span_count
            span_count += 1

            self.assertEqual(len(chunk), 1, "Expected one span in the trace")
            span = chunk[0]

            self.assertEqual(span["trace_id"], self.DISTRIBUTED_TRACE_ID_DEC)
            self.assertEqual(span["parent_id"], self.DISTRIBUTED_PARENT_ID_DEC)
            self.assertEqual(span["metrics"]["_dd.apm.enabled"], 0)

            if span_count == 1:
                self.assertEqual(
                    span["metrics"]["_sampling_priority_v1"],
                    int(self.INJECTED_SAMPLING_PRIORITY_USER_KEEP),
                )
                self.assertNotIn("_dd.p.ts", span.get("meta", {}))
                self.assertNotEqual(span["meta"].get("_dd.p.dm", ""), "-4")
            else:
                self.assertEqual(
                    span["metrics"]["_sampling_priority_v1"],
                    int(self.INJECTED_SAMPLING_PRIORITY_USER_DROP),
                )

            self.assertEqual(
                span.get("meta", {}).get("_dd.origin"),
                self.X_DATADOG_ORIGIN_RUM)

        self.assertTrue(
            self.get_traces(on_chunk),
            "Failed to find traces for distributed_tracing_no_waf",
        )
        self.assertEqual(
            span_count,
            2,
            "Expected to process two spans for distributed_tracing_no_waf",
        )

    def test_distributed_tracing_waf(self):
        headers = {
            "x-datadog-trace-id": self.DISTRIBUTED_TRACE_ID_DEC,
            "x-datadog-parent-id": self.DISTRIBUTED_PARENT_ID_DEC,
            "x-datadog-sampling-priority":
            self.INJECTED_SAMPLING_PRIORITY_AUTO_KEEP,
            "x-datadog-origin": self.X_DATADOG_ORIGIN_RUM,
            "x-datadog-tags": "_dd.p.ts=02",
        }
        for _ in range(2):
            status, _, body = self.orch.send_nginx_http_request(
                "/http", headers=headers)
            self.assertEqual(200, status)

            # Parse the response to check propagation headers
            response = json.loads(body)
            forwarded_headers = response["headers"]

            # Assert that the propagation headers match the expected format
            # The trace ID should remain the same, but parent ID should be different (new span)
            self.assertIn(
                "x-datadog-trace-id",
                forwarded_headers,
                "Missing x-datadog-trace-id header",
            )
            self.assertEqual(
                forwarded_headers["x-datadog-trace-id"],
                str(self.DISTRIBUTED_TRACE_ID_DEC),
                f"x-datadog-trace-id mismatch: expected {self.DISTRIBUTED_TRACE_ID_DEC}, got {forwarded_headers['x-datadog-trace-id']}",
            )

            self.assertIn(
                "x-datadog-parent-id",
                forwarded_headers,
                "Missing x-datadog-parent-id header",
            )
            self.assertNotEqual(
                forwarded_headers["x-datadog-parent-id"],
                str(self.DISTRIBUTED_PARENT_ID_DEC),
                f"x-datadog-parent-id should be different from original {self.DISTRIBUTED_PARENT_ID_DEC}, but got {forwarded_headers['x-datadog-parent-id']}",
            )

            expected_headers = {
                "x-datadog-sampling-priority": "2",
                "x-datadog-origin": "rum",
                "x-datadog-tags": "_dd.p.dm=-5,_dd.p.ts=02",
            }

            for header_name, expected_value in expected_headers.items():
                self.assertIn(header_name, forwarded_headers,
                              f"Missing header: {header_name}")
                self.assertEqual(
                    forwarded_headers[header_name],
                    expected_value,
                    f"Header {header_name} mismatch: expected {expected_value}, got {forwarded_headers[header_name]}",
                )

            # Check traceparent format (trace ID should match, span ID should be different)
            self.assertIn("traceparent", forwarded_headers,
                          "Missing traceparent header")
            traceparent = forwarded_headers["traceparent"]
            traceparent_parts = traceparent.split("-")
            self.assertEqual(len(traceparent_parts), 4,
                             f"Invalid traceparent format: {traceparent}")
            self.assertEqual(
                traceparent_parts[0],
                "00",
                f"Invalid traceparent version: {traceparent_parts[0]}",
            )
            # Convert decimal trace ID to hex and pad to 32 chars
            expected_trace_id_hex = f"{self.DISTRIBUTED_TRACE_ID_DEC:032x}"
            self.assertEqual(
                traceparent_parts[1],
                expected_trace_id_hex,
                f"traceparent trace ID mismatch: expected {expected_trace_id_hex}, got {traceparent_parts[1]}",
            )
            # Span ID should be different from the original parent ID
            original_parent_id_hex = f"{self.DISTRIBUTED_PARENT_ID_DEC:016x}"
            self.assertNotEqual(
                traceparent_parts[2],
                original_parent_id_hex,
                f"traceparent span ID should be different from original {original_parent_id_hex}, but got {traceparent_parts[2]}",
            )
            self.assertEqual(
                traceparent_parts[3],
                "01",
                f"Invalid traceparent flags: {traceparent_parts[3]}",
            )

            # Check tracestate format
            self.assertIn("tracestate", forwarded_headers,
                          "Missing tracestate header")
            tracestate = forwarded_headers["tracestate"]
            # The tracestate should contain the new span ID (same as in traceparent)
            expected_span_id = traceparent_parts[2]
            expected_tracestate = f"dd=s:2;p:{expected_span_id};o:rum;t.dm:-5;t.ts:02"
            self.assertEqual(
                tracestate,
                expected_tracestate,
                f"tracestate mismatch: expected {expected_tracestate}, got {tracestate}",
            )

        span_count = 0

        def on_chunk(chunk):
            nonlocal span_count
            span_count += 1

            self.assertEqual(len(chunk), 1, "Expected one span in the trace")
            span = chunk[0]

            self.assertEqual(span["trace_id"], self.DISTRIBUTED_TRACE_ID_DEC)
            self.assertEqual(span["parent_id"], self.DISTRIBUTED_PARENT_ID_DEC)

            self.assertEqual(span["metrics"]["_dd.apm.enabled"], 0)
            self.assertEqual(span["metrics"]["_sampling_priority_v1"], 2)

            self.assertEqual(span["meta"]["_dd.p.ts"], "02")

            self.assertEqual(
                span.get("meta", {}).get("_dd.origin"),
                self.X_DATADOG_ORIGIN_RUM)

        self.assertTrue(
            self.get_traces(on_chunk),
            "Failed to find traces for distributed_tracing_waf",
        )
        self.assertEqual(
            span_count, 2,
            "Expected to process two spans for distributed_tracing_waf")
