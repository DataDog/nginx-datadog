from pathlib import Path

from .. import case


class TestSecResponseBodyAddress(case.TestCase):
    """Verify which WAF addresses are populated for responses with empty bodies."""

    config_setup_done = False
    requires_waf = True

    def setUp(self):
        super().setUp()
        if not TestSecResponseBodyAddress.config_setup_done:
            waf_path = (Path(__file__).parent /
                        './conf/waf_response_body_address.json')
            self.orch.nginx_replace_file('/tmp/waf.json', waf_path.read_text())

            conf_path = Path(__file__).parent / './conf/http.conf'
            status, log_lines = self.orch.nginx_replace_config(
                conf_path.read_text(), conf_path.name)
            self.assertEqual(0, status, log_lines)

            TestSecResponseBodyAddress.config_setup_done = True

        self.orch.sync_service('agent')

    def test_empty_body_response_body_address_absent(self):
        """server.response.body is not sent to the WAF for empty-body responses."""
        status, _, _ = self.orch.send_nginx_http_request(
            '/http/empty_body_json', 80)
        self.assertEqual(202, status)

        report = self.orch.find_first_appsec_report()
        self.assertIsNotNone(report, 'WAF did not produce an appsec report')

        rule_ids = {t['rule']['id'] for t in report['triggers']}
        self.assertIn(
            '202', rule_ids, 'WAF did not fire on server.response.status; '
            'WAF may not have run at all')
        self.assertNotIn(
            'any_response_body', rule_ids,
            'server.response.body was unexpectedly sent to '
            'the WAF for an empty-body response')
