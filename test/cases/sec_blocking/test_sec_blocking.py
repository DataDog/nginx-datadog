import json

from .. import case

from pathlib import Path


class TestSecBlocking(case.TestCase):
    def run_with_ua(self, user_agent, accept):
        waf_path = Path(__file__).parent / './conf/waf.json'
        waf_text = waf_path.read_text()
        self.orch.nginx_replace_file('/tmp/waf.json', waf_text)

        conf_path = Path(__file__).parent / './conf/http.conf'
        conf_text = conf_path.read_text()

        status, log_lines = self.orch.nginx_replace_config(
            conf_text, conf_path.name)
        self.assertEqual(0, status, log_lines)

        # Consume any previous logging from the agent.
        self.orch.sync_service('agent')

        headers = {'User-Agent': user_agent, 'Accept': accept}
        status, headers, body = self.orch.send_nginx_http_request('/http', 80, headers)

        self.orch.reload_nginx()
        log_lines = self.orch.sync_service('agent')
        headers = dict(headers)
        headers = {k.lower(): v for k, v in headers.items()}
        return status, headers, body, log_lines

    def test_default_action(self):
        status, headers, body, log_lines = self.run_with_ua('block_default', '*/*')
        self.assertEqual(status, 403)
        self.assertEqual(headers['content-type'], 'application/json')
        self.assertRegex(body, r'"title": "You\'ve been blocked')

        traces = [json.loads(line) for line in log_lines]

        def predicate(x):
            return x[0][0]['meta'].get('appsec.blocked') == 'true'

        self.assertTrue(any(predicate(trace) for trace in traces))
