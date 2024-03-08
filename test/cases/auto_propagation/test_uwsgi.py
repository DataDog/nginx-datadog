from .. import case

import json
from pathlib import Path


class TestUWSGI(case.TestCase):

    def test_auto_propagation(self):
        return self.run_test("./conf/uwsgi_auto.conf", should_propagate=True)

    def test_disabled_at_location(self):
        return self.run_test("./conf/uwsgi_disabled_at_location.conf",
                             should_propagate=False)

    def test_disabled_at_server(self):
        return self.run_test("./conf/uwsgi_disabled_at_server.conf",
                             should_propagate=False)

    def test_disabled_at_http(self):
        return self.run_test("./conf/uwsgi_disabled_at_http.conf",
                             should_propagate=False)

    def test_without_module(self):
        return self.run_test("./conf/uwsgi_without_module.conf",
                             should_propagate=False)

    def run_test(self, conf_relative_path, should_propagate):
        conf_path = Path(__file__).parent / conf_relative_path
        conf_text = conf_path.read_text()
        status, log_lines = self.orch.nginx_replace_config(
            conf_text, conf_path.name)
        self.assertEqual(status, 0, log_lines)

        status, _, body = self.orch.send_nginx_http_request("/")
        self.assertEqual(status, 200)
        response = json.loads(body)
        self.assertEqual(response["service"], "uwsgi")
        headers = response["headers"]
        if should_propagate:
            self.assertIn("X-Datadog-Sampling-Priority", headers)
            priority = headers["X-Datadog-Sampling-Priority"]
            priority = int(priority)
        else:
            self.assertNotIn("X-Datadog-Sampling-Priority", headers)
