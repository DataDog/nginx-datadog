import json
from pathlib import Path
import unittest

from .. import case, formats


class TestSecConfig(case.TestCase):
    requires_waf = True

    def apply_config(self, conf_name):
        conf_path = Path(__file__).parent / f"./conf/http_{conf_name}.conf"
        conf_text = conf_path.read_text()
        status, log_lines = self.orch.nginx_replace_config(
            conf_text, conf_path.name)
        self.assertEqual(0, status, log_lines)

    def _test_config(self, conf_name):
        conf_path = Path(__file__).parent / f"./conf/http_{conf_name}.conf"
        conf_text = conf_path.read_text()
        return self.orch.nginx_replace_config(conf_text, conf_path.name)

    def get_appsec_data(self):
        self.orch.reload_nginx()
        log_lines = self.orch.sync_service("agent")
        entries = [
            entry for entry in (formats.parse_trace(line)
                                for line in log_lines) if entry is not None
        ]
        # find _dd.appsec.json in one of the spans of the traces
        for entry in entries:
            for trace in entry:
                for span in trace:
                    if span.get("meta", {}).get("_dd.appsec.json"):
                        return json.loads(span["meta"]["_dd.appsec.json"])
        self.failureException("No _dd.appsec.json found in traces")

    def test_custom_templates(self):
        templ_json_path = Path(__file__).parent / "./conf/templ.json"
        templ_html_path = Path(__file__).parent / "./conf/templ.html"
        self.orch.nginx_replace_file("/tmp/templ.json",
                                     templ_json_path.read_text())
        self.orch.nginx_replace_file("/tmp/templ.html",
                                     templ_html_path.read_text())

        self.apply_config("custom_blocking_templates")

        headers = {
            "User-Agent": "dd-test-scanner-log-block",
            "Accept": "text/html"
        }
        status, headers, body = self.orch.send_nginx_http_request(
            "/http", 80, headers)
        self.assertEqual(status, 403)
        # find content-type header:
        ct = next((v for k, v in headers if k.lower() == "content-type"), None)
        self.assertEqual(ct, "text/html;charset=utf-8")
        self.assertTrue("My custom blocking response" in body)

        headers = {
            "User-Agent": "dd-test-scanner-log-block",
            "Accept": "text/json"
        }
        status, headers, body = self.orch.send_nginx_http_request(
            "/http", 80, headers)
        self.assertEqual(status, 403)
        ct = next((v for k, v in headers if k.lower() == "content-type"), None)
        self.assertEqual(ct, "application/json")
        self.assertEqual(
            body,
            '{"error": "blocked", "details": "my custom json response"}\n')

    def test_appsec_fully_disabled(self):
        self.apply_config("appsec_fully_disabled")

        headers = {
            "User-Agent": "dd-test-scanner-log-block",
            "Accept": "text/json"
        }
        status, _, _ = self.orch.send_nginx_http_request("/", 80, headers)
        self.assertEqual(status, 200)

    def test_bad_custom_template(self):
        # We can't afford to shutdown workers
        status, log_lines = self._test_config("bad_template_file")
        self.assertNotEqual(0, status, log_lines)
        self.assertTrue(
            any('Failed to open file: "/file/that/does/not/exist"' in line
                for line in log_lines))

    def test_bad_rules_file(self):
        status, log_lines = self._test_config("bad_rules_file")
        self.assertNotEqual(0, status, log_lines)
        self.assertTrue(
            any('Failed to open file: "/bad/rules/file' in line
                for line in log_lines))

    def test_bad_pool_name(self):
        status, log_lines = self._test_config("bad_thread_pool")
        self.assertNotEqual(0, status, log_lines)

        self.assertTrue(
            any('datadog_waf_thread_pool_name: "bad_thread_pool" not found' in
                line for line in log_lines))

    def test_multiple_pools(self):
        self.apply_config("multiple_thread_pools")

        headers = {"User-Agent": "dd-test-scanner-log-block"}
        status, _, _ = self.orch.send_nginx_http_request(
            "/http/a", 80, headers)
        self.assertEqual(status, 403)

        headers = {"User-Agent": "dd-test-scanner-log-block"}
        status, _, _ = self.orch.send_nginx_http_request(
            "/local/", 80, headers)
        self.assertEqual(status, 403)

        headers = {"User-Agent": "dd-test-scanner-log-block"}
        status, _, _ = self.orch.send_nginx_http_request(
            "/unmonitored/index.html", 80, headers)
        self.assertEqual(status, 200)

    def test_custom_obfuscation(self):
        waf_path = Path(__file__).parent / "./conf/waf.json"
        waf_text = waf_path.read_text()
        self.orch.nginx_replace_file("/tmp/waf.json", waf_text)

        self.apply_config("custom_obfuscation")

        # Redaction by key
        # datadog_appsec_obfuscation_key_regex my.special.key;
        status, _, _ = self.orch.send_nginx_http_request(
            "/http/?my_special_key=matched+value", 80)
        appsec_data = self.get_appsec_data()
        self.assertEqual(
            appsec_data["triggers"][0]["rule_matches"][0]["parameters"][0]
            ["value"],
            "<Redacted>",
        )

        # Redaction by value
        # datadog_appsec_obfuscation_value_regex \Az.*;
        status, _, _ = self.orch.send_nginx_http_request(
            "/http/?the+key=z_matched+value", 80)
        appsec_data = self.get_appsec_data()
        self.assertEqual(
            appsec_data["triggers"][0]["rule_matches"][0]["parameters"][0]
            ["value"],
            "<Redacted>",
        )

    def test_no_obfuscation(self):
        waf_path = Path(__file__).parent / "./conf/waf.json"
        waf_text = waf_path.read_text()
        self.orch.nginx_replace_file("/tmp/waf.json", waf_text)

        self.apply_config("no_obfuscation")

        self.orch.sync_service("agent")

        # No redaction by key
        status, _, _ = self.orch.send_nginx_http_request(
            "/http/?password=matched+value", 80)
        appsec_data = self.get_appsec_data()
        self.assertEqual(
            appsec_data["triggers"][0]["rule_matches"][0]["parameters"][0]
            ["value"],
            "matched value",
        )
