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

        conf_path = Path(__file__).parent / 'conf' / 'vanilla.conf'
        conf_text = conf_path.read_text()

        status, log_lines = self.orch.nginx_replace_config(
            conf_text, conf_path.name)
        self.assertEqual(0, status, log_lines)

        status, _, body = self.orch.send_nginx_http_request('/')
        self.assertEqual(200, status)

        self.default_config = json.loads(body)
        return self.default_config

    def test_no_config(self):
        # Verify that there _is_ a default config when none is otherwise
        # implied.
        self.get_default_config()

    def test_in_http(self):
        conf_path = Path(__file__).parent / 'conf' / 'in_http.conf'
        conf_text = conf_path.read_text()

        status, log_lines = self.orch.nginx_replace_config(
            conf_text, conf_path.name)
        self.assertEqual(0, status, log_lines)

        status, _, body = self.orch.send_nginx_http_request('/')
        self.assertEqual(200, status)

        config = json.loads(body)
        expected = self.get_default_config()
        # See conf/in_http.conf, which contains the following:
        #
        #     datadog_service_name foosvc;
        #     datadog_environment fooment;
        #     datadog_agent_url http://bogus:1234;
        #     datadog_propagation_styles B3 Datadog;
        expected['defaults']['service'] = 'foosvc'
        expected['defaults']['environment'] = 'fooment'
        expected['collector']['config'][
            'url'] = 'http://bogus:1234/v0.4/traces'
        styles = ['B3', 'Datadog']
        expected['injection_styles'] = styles
        expected['extraction_styles'] = styles

        self.assertEqual(config, expected)

    def run_error_test(self, conf_relative_path, diagnostic_excerpt):
        conf_path = Path(__file__).parent / conf_relative_path
        conf_text = conf_path.read_text()

        status, log_lines = self.orch.nginx_test_config(
            conf_text, conf_path.name)
        self.assertNotEqual(0, status)
        self.assertTrue(any(diagnostic_excerpt in line for line in log_lines),
                        log_lines)

    def test_duplicate_service_name(self):
        self.run_error_test(
            conf_relative_path='./conf/duplicate/service_name.conf',
            diagnostic_excerpt='Duplicate call to "datadog_service_name"')

    def test_duplicate_environment(self):
        self.run_error_test(
            conf_relative_path='./conf/duplicate/environment.conf',
            diagnostic_excerpt='Duplicate call to "datadog_environment"')

    def test_duplicate_agent_url(self):
        self.run_error_test(
            conf_relative_path='./conf/duplicate/agent_url.conf',
            diagnostic_excerpt='Duplicate call to "datadog_agent_url"')

    def test_duplicate_propagation_styles(self):
        self.run_error_test(
            conf_relative_path='./conf/duplicate/propagation_styles.conf',
            diagnostic_excerpt=
            'Datadog propagation styles are already configured.')

    def test_propagation_styles_error(self):
        return self.run_error_test(
            conf_relative_path='./conf/propagation_styles_error.conf',
            diagnostic_excerpt=
            'Datadog propagation styles are already configured.')

    def run_wrong_block_test(self, conf_relative_path):
        conf_path = Path(__file__).parent / conf_relative_path
        conf_text = conf_path.read_text()

        status, log_lines = self.orch.nginx_test_config(
            conf_text, conf_path.name)
        self.assertNotEqual(0, status)
        excerpt = 'directive is not allowed here'
        self.assertTrue(any(excerpt in line for line in log_lines), log_lines)

    def test_error_in_main_service_name(self):
        return self.run_wrong_block_test(
            './conf/error_in_main/service_name.conf')

    def test_error_in_main_environment(self):
        return self.run_wrong_block_test(
            './conf/error_in_main/environment.conf')

    def test_error_in_main_agent_url(self):
        return self.run_wrong_block_test('./conf/error_in_main/agent_url.conf')

    def test_error_in_main_propagation_styles(self):
        return self.run_wrong_block_test(
            './conf/error_in_main/propagation_styles.conf')

    def test_error_in_server_service_name(self):
        return self.run_wrong_block_test(
            './conf/error_in_server/service_name.conf')

    def test_error_in_server_environment(self):
        return self.run_wrong_block_test(
            './conf/error_in_server/environment.conf')

    def test_error_in_server_agent_url(self):
        return self.run_wrong_block_test(
            './conf/error_in_server/agent_url.conf')

    def test_error_in_server_propagation_styles(self):
        return self.run_wrong_block_test(
            './conf/error_in_server/propagation_styles.conf')
