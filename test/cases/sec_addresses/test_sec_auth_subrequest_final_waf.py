"""Regression coverage for final-WAF ownership across subrequests."""

from pathlib import Path

from .. import case


class TestSecAuthSubrequestFinalWaf(case.TestCase):
    requires_waf = True
    config_setup_done = False

    def setUp(self):
        super().setUp()
        if not TestSecAuthSubrequestFinalWaf.config_setup_done:
            conf_dir = Path(__file__).parent / "conf"
            waf_path = conf_dir / "waf_auth_subrequest_final.json"
            self.orch.nginx_replace_file("/tmp/waf.json", waf_path.read_text())

            conf_path = conf_dir / "http_auth_subrequest_final_waf.conf"
            status, log_lines = self.orch.nginx_replace_config(
                conf_path.read_text(), conf_path.name)
            self.assertEqual(0, status, log_lines)

            TestSecAuthSubrequestFinalWaf.config_setup_done = True

        self.orch.sync_service("agent")

    def test_final_waf_runs_for_main_request_not_auth_subrequest(self):
        # The auth subrequest returns 204 while the main upstream returns 200.
        # The ruleset has one server.response.status rule for each response,
        # so the rule recorded in the AppSec report identifies which request
        # supplied the response data to the one final WAF run. The correct
        # result is main-response-status only; auth-subrequest-status means
        # the traced auth subrequest incorrectly consumed the shared final
        # WAF run before the main response reached the header filter.
        status, _, _ = self.orch.send_nginx_http_request("/http", 80)

        report = self.orch.find_first_appsec_report()
        self.assertIsNotNone(report, "final WAF did not produce a report")

        rule_ids = {trigger["rule"]["id"] for trigger in report["triggers"]}
        self.assertNotIn(
            "auth-subrequest-status",
            rule_ids,
            "the auth subrequest incorrectly owned the final WAF run",
        )
        self.assertIn(
            "main-response-status",
            rule_ids,
            "the main response did not own the final WAF run",
        )
        self.assertEqual(200, status)
