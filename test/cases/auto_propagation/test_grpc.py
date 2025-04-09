from .. import case
import unittest

import json
from pathlib import Path


class TestGRPC(case.TestCase):

    def test_auto_propagation(self):
        conf_path = Path(__file__).parent / "./conf/grpc_auto.conf"
        conf_text = conf_path.read_text()
        status, log_lines = self.orch.nginx_replace_config(
            conf_text, conf_path.name, False)
        self.assertEqual(status, 0, log_lines)

        status, body = self.orch.send_nginx_grpc_request(
            "upstream.Upstream.GetMetadata", port=1337)
        self.assertEqual(status, 0, body)
        response = json.loads(body)
        self.assertEqual(response["service"], "grpc")
        metadata = response["metadata"]

        self.assertIn("server-block-header", metadata)
        self.assertEqual("not-hidden-by-autoinjection",
                         metadata["server-block-header"])

        self.assertIn("x-datadog-sampling-priority", metadata)
        priority = metadata["x-datadog-sampling-priority"]
        priority = int(priority)

    def test_disabled_at_location(self):
        return self.run_test("./conf/grpc_disabled_at_location.conf",
                             should_propagate=False)

    def test_disabled_at_server(self):
        return self.run_test("./conf/grpc_disabled_at_server.conf",
                             should_propagate=False)

    def test_disabled_at_http(self):
        return self.run_test("./conf/grpc_disabled_at_http.conf",
                             should_propagate=False)

    @unittest.skip("")
    def test_without_module(self):
        return self.run_test("./conf/grpc_without_module.conf",
                             should_propagate=False)

    def run_test(self, conf_relative_path, should_propagate):
        conf_path = Path(__file__).parent / conf_relative_path
        conf_text = conf_path.read_text()
        status, log_lines = self.orch.nginx_replace_config(
            conf_text, conf_path.name, False)
        self.assertEqual(status, 0, log_lines)

        status, body = self.orch.send_nginx_grpc_request(
            "upstream.Upstream.GetMetadata", port=1337)
        self.assertEqual(status, 0, body)
        response = json.loads(body)
        self.assertEqual(response["service"], "grpc")
        metadata = response["metadata"]
        if should_propagate:
            self.assertIn("x-datadog-sampling-priority", metadata)
            priority = metadata["x-datadog-sampling-priority"]
            priority = int(priority)
        else:
            self.assertNotIn("x-datadog-sampling-priority", metadata)
