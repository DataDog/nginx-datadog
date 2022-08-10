from .. import case

import json
from pathlib import Path


class TestConfiguration(case.TestCase):

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.default_config = None

    def get_default_config(self):
        if self.default_config is not None:
            return self.default_config

        conf_path = Path(__file__).parent / './conf/vanilla.conf'
        conf_text = conf_path.read_text()

        status, log_lines = self.orch.nginx_replace_config(
            conf_text, conf_path.name)
        self.assertEqual(0, status, log_lines)

        status, body = self.orch.send_nginx_http_request('/')
        self.assertEqual(200, status)

        self.default_config = json.loads(body)
        return self.default_config

    def test_no_config(self):
        # Verify that there _is_ a default config when none is otherwise
        # implied.
        self.get_default_config()

    def run_explicit_config_test(self, conf_relative_path):
        conf_path = Path(__file__).parent / conf_relative_path
        conf_text = conf_path.read_text()

        status, log_lines = self.orch.nginx_replace_config(
            conf_text, conf_path.name)
        self.assertEqual(0, status, log_lines)

        status, body = self.orch.send_nginx_http_request('/')
        self.assertEqual(200, status)

        config = json.loads(body)
        expected = {**self.get_default_config(), "tags": {"foo": "bar"}}
        self.assertEqual(config, expected)

    def test_explicit_in_http(self):
        return self.run_explicit_config_test('./conf/explicit_in_http.conf')

    def run_wrong_block_error_test(self, conf_relative_path):
        conf_path = Path(__file__).parent / conf_relative_path
        conf_text = conf_path.read_text()

        status, log_lines = self.orch.nginx_test_config(
            conf_text, conf_path.name)
        self.assertNotEqual(0, status)
        excerpt = '"datadog" directive is not allowed here'
        self.assertTrue(any(excerpt in line for line in log_lines), log_lines)

    def test_error_in_server(self):
        return self.run_wrong_block_error_test('./conf/error_in_server.conf')

    def test_error_in_main(self):
        return self.run_wrong_block_error_test('./conf/error_in_main.conf')

    def test_duplicate_ok(self):
        return self.run_explicit_config_test('./conf/duplicate_ok.conf')

    def test_duplicate_error(self):
        conf_path = Path(__file__).parent / './conf/duplicate_error.conf'
        conf_text = conf_path.read_text()

        status, log_lines = self.orch.nginx_test_config(
            conf_text, conf_path.name)
        self.assertNotEqual(0, status)
        excerpt = 'Datadog tracing is already configured.  It was default-configured by the call to "proxy_pass"'
        self.assertTrue(any(excerpt in line for line in log_lines), log_lines)
