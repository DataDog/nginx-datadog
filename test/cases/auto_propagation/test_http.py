from .. import case

import json
from pathlib import Path


class TestHTTP(case.TestCase):
    def test_auto_propagation(self):
        return self.run_test("./conf/http_auto.conf", should_propagate=True)

    def test_auto_propagation_not_shadowing_proxy_set_header(self):
        conf_path = Path(__file__).parent / "./conf/http_auto_not_shadowing.conf"
        conf_text = conf_path.read_text()
        status, log_lines = self.orch.nginx_replace_config(conf_text, conf_path.name)
        self.assertEqual(status, 0, log_lines)

        status, body = self.orch.send_nginx_http_request("/http")
        self.assertEqual(status, 200)
        response = json.loads(body)
        self.assertEqual(response["service"], "http")
        headers = response["headers"]
        self.assertIn("x-datadog-sampling-priority", headers)
        priority = headers["x-datadog-sampling-priority"]
        priority = int(priority)

        self.assertIn("server-proxy-header", headers)
        self.assertEqual("not-shadowing", headers["server-proxy-header"])

    def test_disabled_at_location(self):
        return self.run_test(
            "./conf/http_disabled_at_location.conf", should_propagate=False
        )

    def test_disabled_at_server(self):
        return self.run_test(
            "./conf/http_disabled_at_server.conf", should_propagate=False
        )

    def test_disabled_at_http(self):
        return self.run_test(
            "./conf/http_disabled_at_http.conf", should_propagate=False
        )

    def test_without_module(self):
        return self.run_test("./conf/http_without_module.conf", should_propagate=False)

    def run_test(self, conf_relative_path, should_propagate):
        conf_path = Path(__file__).parent / conf_relative_path
        conf_text = conf_path.read_text()
        status, log_lines = self.orch.nginx_replace_config(conf_text, conf_path.name)
        self.assertEqual(status, 0, log_lines)

        status, body = self.orch.send_nginx_http_request("/http")
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
