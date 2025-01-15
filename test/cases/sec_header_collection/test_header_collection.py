import json

from .. import case

from pathlib import Path


@case.skipUnlessWaf
class TestHeaderCollection(case.TestCase):
    config_setup_done = False

    req_headers_unconditional = [
        "Content-Type",
        "User-agent",
        "Accept",
        "X-amzn-trace-id",
        "Cloudfront-Viewer-JA3-Fingerprint",
        "Cf-Ray",
        "X-Cloud-Trace-Context",
        "X-Appgw-Trace-Id",
        "X-Sigsci-Requestid",
        "X-Sigsci-Tags",
        "Akamai-User-Risk",
    ]

    req_headers_ip = [
        "x-forwarded-for",
        "x-real-ip",
        "true-client-ip",
        "x-client-ip",
        "x-forwarded",
        "forwarded-for",
        "x-cluster-client-ip",
        "fastly-client-ip",
        "cf-connecting-ip",
        "cf-connecting-ipv6",
        "forwarded",
        "via",
    ]

    req_headers_attack = req_headers_ip + [
        "content-length",
        "content-encoding",
        "content-language",
        "host",
        "accept-encoding",
        "accept-language",
    ]

    resp_headers = [
        "content-length",
        "content-type",
        "content-encoding",
        "content-language",
    ]

    def setUp(self):
        super().setUp()
        # avoid reconfiguration (cuts time almost in half)
        if not TestHeaderCollection.config_setup_done:
            waf_path = Path(__file__).parent / "./conf/waf.json"
            waf_text = waf_path.read_text()
            self.orch.nginx_replace_file("/tmp/waf.json", waf_text)

            conf_path = Path(__file__).parent / "./conf/http.conf"
            conf_text = conf_path.read_text()

            status, log_lines = self.orch.nginx_replace_config(
                conf_text, conf_path.name)
            self.assertEqual(0, status, log_lines)

            TestHeaderCollection.config_setup_done = True

        # Consume any previous logging from the agent.
        self.orch.sync_service("agent")

    def get_meta(self):
        self.orch.reload_nginx()
        log_lines = self.orch.sync_service("agent")
        entries = [
            json.loads(line) for line in log_lines if line.startswith("[[{")
            if "_dd.appsec.waf.version" in line
        ]
        self.assertEqual(len(entries), 1)

        return entries[0][0][0]["meta"]

    def test_no_req_attack(self):
        headers = {
            header: f"value for {header}"
            for header in TestHeaderCollection.req_headers_unconditional
        }
        status, _, _ = self.orch.send_nginx_http_request("/http",
                                                         80,
                                                         headers=headers)
        self.assertEqual(status, 200)

        meta = self.get_meta()

        # check each of the headers in headers_unconditional
        for header in TestHeaderCollection.req_headers_unconditional:
            self.assertEqual(f"value for {header}",
                             meta[f"http.request.headers.{header.lower()}"])

        # check that the headers in headers_attack are not present
        for header in TestHeaderCollection.req_headers_attack:
            self.assertNotIn(f"http.request.headers.{header.lower()}", meta)

    def test_req_attack(self):
        headers = {
            header: f"value for {header}"
            for header in TestHeaderCollection.req_headers_unconditional
        }
        for header in TestHeaderCollection.req_headers_ip:
            headers[header] = "1.2.3.4"

        headers["content-language"] = "en-US"
        headers["content-encoding"] = "identity"
        headers["accept-language"] = "en-US"
        headers["accept-encoding"] = "identity"

        status, _, _ = self.orch.send_nginx_http_request(
            "/http?a=matched+value",
            80,
            headers=headers,
            req_body="foobar",
            method="POST",
        )
        self.assertEqual(status, 200)

        meta = self.get_meta()

        # check each of the headers in headers_unconditional
        for header in TestHeaderCollection.req_headers_unconditional:
            self.assertEqual(f"value for {header}",
                             meta[f"http.request.headers.{header.lower()}"])

        # ip headers are present
        for header in TestHeaderCollection.req_headers_ip:
            self.assertEqual("1.2.3.4",
                             meta[f"http.request.headers.{header.lower()}"])

        self.assertEqual("en-US",
                         meta["http.request.headers.content-language"])
        self.assertEqual("identity",
                         meta["http.request.headers.content-encoding"])
        self.assertEqual("en-US", meta["http.request.headers.accept-language"])
        self.assertEqual("identity",
                         meta["http.request.headers.accept-encoding"])
        self.assertIn("http.request.headers.host", meta)
        self.assertIn("http.request.headers.content-length", meta)

    def test_resp_no_attack(self):
        status, _, _ = self.orch.send_nginx_http_request("/http", 80)
        self.assertEqual(status, 200)

        meta = self.get_meta()

        # check each of the headers in resp_headers
        self.assertEqual("text/plain",
                         meta["http.response.headers.content-type"])
        # content-length is not present in the response
        # self.assertIn("http.response.headers.content-length", meta)
        self.assertEqual("identity",
                         meta["http.response.headers.content-encoding"])
        self.assertEqual("pt_PT",
                         meta["http.response.headers.content-language"])
