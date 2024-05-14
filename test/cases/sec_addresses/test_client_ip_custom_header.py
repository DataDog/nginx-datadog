import json
from pathlib import Path

from .. import case


class TestClientIpCustomHeader(case.TestCase):
    config_setup_done = False
    requires_waf = True

    def setUp(self):
        super().setUp()
        if self.waf_disabled:
            return

        # avoid reconfiguration (cuts time almost in half)
        if not TestClientIpCustomHeader.config_setup_done:
            waf_path = Path(__file__).parent / './conf/waf.json'
            waf_text = waf_path.read_text()
            self.orch.nginx_replace_file('/tmp/waf.json', waf_text)

            conf_path = Path(
                __file__).parent / './conf/http_custom_header.conf'
            conf_text = conf_path.read_text()
            status, log_lines = self.orch.nginx_replace_config(
                conf_text, conf_path.name)
            self.assertEqual(0, status, log_lines)

            TestClientIpCustomHeader.config_setup_done = True

        # Consume any previous logging from the agent.
        self.orch.sync_service('agent')

    def get_appsec_data(self):
        rep = self.orch.find_first_appsec_report()
        if rep is None:
            self.failureException('No _dd.appsec.json found in traces')
        return rep

    def test_ipv4_from_custom_header(self):
        headers = {'My-Header-IP': '1.2.3.4'}
        status, _, _ = self.orch.send_nginx_http_request('/', 80, headers)
        self.assertEqual(status, 200)

        appsec_data = self.get_appsec_data()
        self.assertEqual(
            appsec_data['triggers'][0]['rule_matches'][0]['parameters'][0]
            ['value'], '1.2.3.4')

    def test_ipv4_from_custom_header_for_variant(self):
        headers = {'My-Header-IP': 'for=1.2.3.4'}
        status, _, _ = self.orch.send_nginx_http_request('/', 80, headers)
        self.assertEqual(status, 200)

        appsec_data = self.get_appsec_data()
        self.assertEqual(
            appsec_data['triggers'][0]['rule_matches'][0]['parameters'][0]
            ['value'], '1.2.3.4')

    def test_ipv4_from_custom_header_port_variant(self):
        headers = {'My-Header-IP': '1.2.3.4:1234'}
        status, _, _ = self.orch.send_nginx_http_request('/', 80, headers)
        self.assertEqual(status, 200)

        appsec_data = self.get_appsec_data()
        self.assertEqual(
            appsec_data['triggers'][0]['rule_matches'][0]['parameters'][0]
            ['value'], '1.2.3.4')

    def test_ipv6_from_custom_header(self):
        headers = {'My-Header-IP': 'fe80:00::1'}
        status, _, _ = self.orch.send_nginx_http_request('/', 80, headers)
        self.assertEqual(status, 200)

        appsec_data = self.get_appsec_data()
        self.assertEqual(
            appsec_data['triggers'][0]['rule_matches'][0]['parameters'][0]
            ['value'], 'fe80::1')

    def test_other_headers_are_ignored(self):
        headers = {'x-real-ip': 'fe80:00::1'}
        status, _, _ = self.orch.send_nginx_http_request('/', 80, headers)
        self.assertEqual(status, 200)

        appsec_data = self.get_appsec_data()
        self.assertEqual(None, appsec_data)
