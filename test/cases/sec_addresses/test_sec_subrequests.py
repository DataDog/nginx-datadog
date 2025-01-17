from pathlib import Path

from .. import case


@case.skipUnlessWaf
class TestSecSubRequests(case.TestCase):
    config_setup_done = False

    def setUp(self):
        super().setUp()
        if not TestSecSubRequests.config_setup_done:
            waf_path = Path(__file__).parent / "./conf/waf.json"
            waf_text = waf_path.read_text()
            self.orch.nginx_replace_file("/tmp/waf.json", waf_text)

            conf_path = Path(
                __file__).parent / "./conf/http_auth_subrequest.conf"
            conf_text = conf_path.read_text()

            status, log_lines = self.orch.nginx_replace_config(
                conf_text, conf_path.name)
            self.assertEqual(0, status, log_lines)

            TestSecSubRequests.config_setup_done = True

        # Consume any previous logging from the agent.
        self.orch.sync_service("agent")

    def get_appsec_data(self):
        rep = self.orch.find_first_appsec_report()
        if rep is None:
            self.failureException("No _dd.appsec.json found in traces")
        return rep

    def test_req_attack(self):
        status, _, _ = self.orch.send_nginx_http_request(
            "/http?a=matched+value", 80, headers={"x-token": "mysecret"})
        self.assertEqual(status, 200)

        appsec_data = self.get_appsec_data()
        self.assertEqual(
            appsec_data["triggers"][0]["rule_matches"][0]["parameters"][0]
            ["value"],
            "matched value",
        )
