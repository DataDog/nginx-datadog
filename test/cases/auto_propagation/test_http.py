from .. import case

import json
from pathlib import Path


class TestHTTP(case.TestCase):

    def test_auto_propagation(self):
        conf_path = Path(__file__).parent / "./conf/http_auto.conf"
        conf_text = conf_path.read_text()
        status, log_lines = self.orch.nginx_replace_config(
            conf_text, conf_path.name)
        self.assertEqual(status, 0, log_lines)

        status, _, body = self.orch.send_nginx_http_request("/http")
        self.assertEqual(status, 200)
        response = json.loads(body)
        self.assertEqual(response["service"], "http")
        headers = response["headers"]

        self.assertIn("server-block-header", headers)
        self.assertEqual("not-hidden-by-autoinjection",
                         headers["server-block-header"])

        self.assertIn("x-datadog-sampling-priority", headers)
        priority = headers["x-datadog-sampling-priority"]
        priority = int(priority)

    def test_context_extraction(self):
        conf_path = Path(__file__).parent / "./conf/http_auto.conf"
        conf_text = conf_path.read_text()
        status, log_lines = self.orch.nginx_replace_config(
            conf_text, conf_path.name)
        self.assertEqual(status, 0, log_lines)

        tracing_context_headers = {
            "x-datadog-trace-id": "2993963891409991723",
            "x-datadog-parent-id": "6383613330463382713",
        }

        status, _, body = self.orch.send_nginx_http_request(
            "/http", headers=tracing_context_headers)
        self.assertEqual(status, 200)
        response = json.loads(body)
        self.assertEqual(response["service"], "http")
        headers = response["headers"]

        self.assertEqual(tracing_context_headers["x-datadog-trace-id"],
                         headers["x-datadog-trace-id"])
        self.assertNotEqual(
            tracing_context_headers["x-datadog-parent-id"],
            headers["x-datadog-parent-id"],
        )

    def test_disabled_at_location(self):
        return self.run_test("./conf/http_disabled_at_location.conf",
                             should_propagate=False)

    def test_disabled_at_server(self):
        return self.run_test("./conf/http_disabled_at_server.conf",
                             should_propagate=False)

    def test_disabled_at_http(self):
        return self.run_test("./conf/http_disabled_at_http.conf",
                             should_propagate=False)

    def test_without_module(self):
        return self.run_test("./conf/http_without_module.conf",
                             should_propagate=False)

    def test_illformated_request(self):
        # From: <https://github.com/DataDog/nginx-datadog/issues/212>
        conf_path = Path(__file__).parent / "./conf/http_auto.conf"
        conf_text = conf_path.read_text()
        status, log_lines = self.orch.nginx_replace_config(
            conf_text, conf_path.name)
        self.assertEqual(status, 0, log_lines)

        status, response = self.orch.send_nginx_raw_http_request(
            request_line="GET /\r\n")
        self.assertEqual(0, status)
        self.assertEqual("Hello, World!", response)

    def run_test(self, conf_relative_path, should_propagate):
        conf_path = Path(__file__).parent / conf_relative_path
        conf_text = conf_path.read_text()
        status, log_lines = self.orch.nginx_replace_config(
            conf_text, conf_path.name)
        self.assertEqual(status, 0, log_lines)

        status, _, body = self.orch.send_nginx_http_request("/http")
        self.assertEqual(status, 200)
        response = json.loads(body)
        self.assertEqual(response["service"], "http")
        headers = response["headers"]
        if should_propagate:
            self.assertIn("x-datadog-sampling-priority", headers)
            priority = headers["x-datadog-sampling-priority"]
            priority = int(priority)
        else:
            self.assertNotIn("x-datadog-sampling-priority", headers)
