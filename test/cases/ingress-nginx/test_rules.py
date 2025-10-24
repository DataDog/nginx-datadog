from .. import case

import json
import os
from pathlib import Path
from unittest import skipUnless


@skipUnless(
    os.getenv("NGINX_FLAVOR", "") == "ingress-nginx",
    "Test embedded sampling rules only for ingress-nginx",
)
class TestIngressNginxRules(case.TestCase):

    def test_healthcheck(self):
        # Healthcheck request MUST not be generated traces.
        conf_path = Path(__file__).parent / "conf" / "main.conf"
        conf_text = conf_path.read_text()

        status, log_lines = self.orch.nginx_replace_config(
            conf_text, conf_path.name)
        self.assertEqual(status, 0, log_lines)

        status, _, body = self.orch.send_nginx_http_request(
            "/is-dynamic-lb-initialized")
        self.assertEqual(status, 200)

        response = json.loads(body)
        self.assertEqual(response["headers"]["x-datadog-sampling-priority"],
                         "-1")

        status, _, body = self.orch.send_nginx_http_request("/")
        self.assertEqual(status, 200)

        response = json.loads(body)
        self.assertEqual(response["headers"]["x-datadog-sampling-priority"],
                         "1")

    def test_healthcheck_stubstatus(self):
        # Healthcheck request MUST not be generated traces.
        conf_path = Path(__file__).parent / "conf" / "main.conf"
        conf_text = conf_path.read_text()

        status, log_lines = self.orch.nginx_replace_config(
            conf_text, conf_path.name)
        self.assertEqual(status, 0, log_lines)

        status, _, body = self.orch.send_nginx_http_request(
            "/nginx_status")
        self.assertEqual(status, 200)

        response = json.loads(body)
        self.assertEqual(response["headers"]["x-datadog-sampling-priority"],
                         "-1")

        status, _, body = self.orch.send_nginx_http_request("/")
        self.assertEqual(status, 200)

        response = json.loads(body)
        self.assertEqual(response["headers"]["x-datadog-sampling-priority"],
                         "1")
