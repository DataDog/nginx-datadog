import json

from .. import case

from pathlib import Path


class TestSecBlocking(case.TestCase):
    config_setup_done = False

    def setUp(self):
        super().setUp()
        self.requires_waf()
        # avoid reconfiguration (cuts time almost in half)
        if not TestSecBlocking.config_setup_done:
            waf_path = Path(__file__).parent / './conf/waf.json'
            waf_text = waf_path.read_text()
            self.orch.nginx_replace_file('/tmp/waf.json', waf_text)

            conf_path = Path(__file__).parent / './conf/http.conf'
            conf_text = conf_path.read_text()

            status, log_lines = self.orch.nginx_replace_config(
                conf_text, conf_path.name)
            self.assertEqual(0, status, log_lines)

            TestSecBlocking.config_setup_done = True

        # Consume any previous logging from the agent.
        self.orch.sync_service('agent')

    def run_with_ua(self, user_agent, accept):
        headers = {'User-Agent': user_agent, 'Accept': accept}
        status, headers, body = self.orch.send_nginx_http_request(
            '/http', 80, headers)

        self.orch.reload_nginx()
        log_lines = self.orch.sync_service('agent')
        headers = dict(headers)
        headers = {k.lower(): v for k, v in headers.items()}
        return status, headers, body, log_lines

    def test_default_action(self):
        status, headers, body, log_lines = self.run_with_ua(
            'block_default', '*/*')
        self.assertEqual(status, 403)
        self.assertEqual(headers['content-type'], 'application/json')
        self.assertRegex(body, r'"title": "You\'ve been blocked')

        traces = [json.loads(line) for line in log_lines]

        def predicate(x):
            return x[0][0]['meta'].get('appsec.blocked') == 'true'

        self.assertTrue(any(predicate(trace) for trace in traces))

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

    def test_redirect_bad_status(self):
        status, headers, _, _ = self.run_with_ua('redirect_bad_status', '*/*')
        self.assertEqual(status, 303)
        self.assertEqual(headers['location'], 'https://www.cloudflare.com')
