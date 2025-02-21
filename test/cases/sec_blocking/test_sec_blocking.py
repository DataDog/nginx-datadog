import json

from .. import case

from pathlib import Path


class TestSecBlocking(case.TestCase):
    config_setup_done = False
    requires_waf = True
    min_nginx_version = '1.26.0'

    def setUp(self):
        super().setUp()
        # avoid reconfiguration (cuts time almost in half)
        if not TestSecBlocking.config_setup_done:
            waf_path = Path(__file__).parent / './conf/waf.json'
            waf_text = waf_path.read_text()
            self.orch.nginx_replace_file('/tmp/waf.json', waf_text)

            conf_path = Path(__file__).parent / './conf/http.conf'
            conf_text = conf_path.read_text()

            crt_path = Path(__file__).parent / './cert/example.com.crt'
            crt_text = crt_path.read_text()
            self.orch.nginx_replace_file('/tmp/example.com.crt', crt_text)

            key_path = Path(__file__).parent / './cert/example.com.key'
            key_text = key_path.read_text()
            self.orch.nginx_replace_file('/tmp/example.com.key', key_text)

            status, log_lines = self.orch.nginx_replace_config(
                conf_text, conf_path.name)
            self.assertEqual(0, status, log_lines)

            TestSecBlocking.config_setup_done = True

        # Consume any previous logging from the agent.
        self.orch.sync_service('agent')

    @staticmethod
    def convert_headers(headers):
        return {k.lower(): v for k, v in dict(headers).items()}

    def run_with_ua(self, user_agent, accept, http_version=1):
        headers = {'User-Agent': user_agent, 'Accept': accept}
        if http_version == 3:
            status, headers, body = self.orch.send_nginx_http_request(
                '/http', tls=True, port=443, headers=headers, http_version=3)
        else:
            status, headers, body = self.orch.send_nginx_http_request(
                '/http', 80, headers, http_version=http_version)

        self.orch.reload_nginx()
        log_lines = self.orch.sync_service('agent')
        return status, TestSecBlocking.convert_headers(
            headers), body, log_lines

    def run_with_body(self, content_type, req_body, http_version=1):
        if http_version == 3:
            port, tls = 443, True
        else:
            port, tls = 80, False
        status, headers, body = self.orch.send_nginx_http_request(
            '/http',
            port=port,
            tls=tls,
            headers={'content-type': content_type},
            req_body=req_body,
            http_version=http_version)

        self.orch.reload_nginx()
        log_lines = self.orch.sync_service('agent')
        headers = dict(headers)
        headers = {k.lower(): v for k, v in headers.items()}
        return status, headers, body, log_lines

    def assert_has_report(self, log_lines, exp_match='block'):
        traces = [
            json.loads(line) for line in log_lines if line.startswith('[[{')
        ]

        def predicate(x):
            return x[0][0]['meta'].get('appsec.blocked') == 'true'

        trace = next((trace for trace in traces if predicate(trace)), None)
        if trace is None:
            self.fail('No trace found with appsec.blocked=true')

        # check if we also get appsec report
        if '_dd.appsec.json' not in trace[0][0]['meta']:
            self.fail('No appsec report found in trace')

        appsec_rep = json.loads(trace[0][0]['meta']['_dd.appsec.json'])

        self.assertEqual(appsec_rep['triggers'][0]['rule']['on_match'][0],
                         exp_match)

    def test_default_action(self):
        status, headers, body, log_lines = self.run_with_ua(
            'block_default', '*/*')
        self.assertEqual(status, 403)
        self.assertEqual(headers['content-type'], 'application/json')
        self.assertRegex(body, r'"title":"You\'ve been blocked')
        self.assert_has_report(log_lines)

    def test_default_action_html(self):
        status, headers, body, _ = self.run_with_ua('block_default',
                                                    'text/html')
        self.assertEqual(status, 403)
        self.assertEqual(headers['content-type'], 'text/html;charset=utf-8')
        self.assertRegex(body, r'<title>You\'ve been blocked')

    def test_default_action_html_quality(self):
        status, headers, body, _ = self.run_with_ua(
            'block_default', 'application/json;q=0.5, text/html')
        self.assertEqual(status, 403)
        self.assertEqual(headers['content-type'], 'text/html;charset=utf-8')
        self.assertRegex(body, r'<title>You\'ve been blocked')

    def test_default_action_html_specificity(self):
        status, headers, body, _ = self.run_with_ua(
            'block_default', 'application/json;q=0.5,text/html;q=0.6,'
            'text/*;q=0.4')
        self.assertEqual(status, 403)
        self.assertEqual(headers['content-type'], 'text/html;charset=utf-8')
        self.assertRegex(body, r'<title>You\'ve been blocked')

    def test_default_action_html_order(self):
        status, headers, body, _ = self.run_with_ua(
            'block_default', 'text/html, application/json')
        self.assertEqual(status, 403)
        self.assertEqual(headers['content-type'], 'text/html;charset=utf-8')
        self.assertRegex(body, r'<title>You\'ve been blocked')

    def test_html_action(self):
        status, headers, body, _ = self.run_with_ua('block_html',
                                                    'application/json')
        self.assertEqual(status, 403)
        self.assertEqual(headers['content-type'], 'text/html;charset=utf-8')

    def test_html_action_http2(self):
        status, headers, body, _ = self.run_with_ua('block_html',
                                                    'application/json', http_version=2)
        self.assertEqual(status, 403)
        self.assertEqual(headers['content-type'], 'text/html;charset=utf-8')

    def test_html_action_http3(self):
        status, headers, body, _ = self.run_with_ua('block_html',
                                                    'application/json', http_version=3)
        self.assertEqual(status, 403)
        self.assertEqual(headers['content-type'], 'text/html;charset=utf-8')

    def test_json_action(self):
        status, headers, body, _ = self.run_with_ua('block_json', 'text/html')
        self.assertEqual(status, 403)
        self.assertEqual(headers['content-type'], 'application/json')

    def test_block_alt_code(self):
        status, headers, body, _ = self.run_with_ua('block_alt_code', '*/*')
        self.assertEqual(status, 501)
        self.assertEqual(headers['content-type'], 'application/json')

    def test_redirect_action(self):
        status, headers, _, _ = self.run_with_ua('redirect', '*/*')
        self.assertEqual(status, 301)
        self.assertEqual(headers['location'], 'https://www.cloudflare.com')

    def test_redirect_action_http2(self):
        status, headers, _, _ = self.run_with_ua('redirect', '*/*', http_version=2)
        self.assertEqual(status, 301)
        self.assertEqual(headers['location'], 'https://www.cloudflare.com')

    def test_redirect_action_http3(self):
        status, headers, _, _ = self.run_with_ua('redirect', '*/*', http_version=3)
        self.assertEqual(status, 301)
        self.assertEqual(headers['location'], 'https://www.cloudflare.com')

    def test_redirect_bad_status(self):
        status, headers, _, _ = self.run_with_ua('redirect_bad_status', '*/*')
        self.assertEqual(status, 303)
        self.assertEqual(headers['location'], 'https://www.cloudflare.com')

    def test_block_body_json(self):
        status, _, _, log_lines = self.run_with_body('application/json',
                                                     '{"a": "block_default"}')
        self.assertEqual(status, 403)
        self.assert_has_report(log_lines)

    def test_block_body_json_long(self):
        status, _, _, log_lines = self.run_with_body(
            'application/json',
            '{"a": "block_default", "b": "' + ('a' * 1024 * 1024))
        self.assertEqual(status, 403)
        self.assert_has_report(log_lines)

    def test_block_body_json_long_http2(self):
        status, _, _, log_lines = self.run_with_body(
            'application/json',
            '{"a": "block_default", "b": "' + ('a' * 1024 * 1024), http_version=2)
        self.assertEqual(status, 403)
        self.assert_has_report(log_lines)

    def test_block_body_json_long_http3(self):
        status, _, _, log_lines = self.run_with_body(
            'application/json',
            '{"a": "block_default", "b": "' + ('a' * 1024 * 1024), http_version=3)
        self.assertEqual(status, 403)
        self.assert_has_report(log_lines)

    def block_on_status(self, http_version):
        if http_version != 3:
            status, headers, body = self.orch.send_nginx_http_request(
                '/http/status/410', http_version=http_version)
        else:
            status, headers, body = self.orch.send_nginx_http_request(
                '/http/status/410', tls=True, port=443, http_version=3)
        self.orch.reload_nginx()
        log_lines = self.orch.sync_service('agent')
        self.assertEqual(501, status)
        headers = TestSecBlocking.convert_headers(headers)
        self.assertEqual(headers['content-type'], 'application/json')
        self.assertRegex(body, r'"title":"You\'ve been blocked')
        self.assert_has_report(log_lines, 'block_501')

    def test_block_on_status_http11(self):
        self.block_on_status(1)

    def test_block_on_status_http2(self):
        self.block_on_status(2)

    def test_block_on_status_http3(self):
        self.block_on_status(3)


    def block_on_status_redirect(self, http_version):
        if http_version != 3:
            status, headers, body = self.orch.send_nginx_http_request(
                '/http/status/411', http_version=http_version)
        else:
            status, headers, body = self.orch.send_nginx_http_request(
                '/http/status/411', tls=True, port=443, http_version=3)
        self.orch.reload_nginx()
        log_lines = self.orch.sync_service('agent')
        self.assertEqual(301, status)
        headers = TestSecBlocking.convert_headers(headers)
        self.assertEqual(headers['location'], 'https://www.cloudflare.com')
        self.assert_has_report(log_lines, 'redirect')

    def test_block_on_status_redirect_http11(self):
        self.block_on_status(1)

    def test_block_on_status_redirect_http2(self):
        self.block_on_status(2)

    def test_block_on_status_redirect_http3(self):
        self.block_on_status(3)

    def test_block_on_response_header(self):
        nginx_version = self.orch.nginx_version()
        status, headers, body = self.orch.send_nginx_http_request(
            '/resp_header_blocked')
        self.orch.reload_nginx()
        log_lines = self.orch.sync_service('agent')
        self.assertEqual(501, status)
        headers = TestSecBlocking.convert_headers(headers)
        self.assertEqual(headers['content-type'], 'application/json')
        self.assertRegex(body, r'"title":"You\'ve been blocked')
        self.assert_has_report(log_lines, 'block_501')
