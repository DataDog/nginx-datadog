import json

from .. import case

from pathlib import Path


class TestSecAddresses(case.TestCase):
    config_setup_done = False

    def setUp(self):
        super().setUp()
        # avoid reconfiguration (cuts time almost in half)
        if not TestSecAddresses.config_setup_done:
            waf_path = Path(__file__).parent / './conf/waf.json'
            waf_text = waf_path.read_text()
            self.orch.nginx_replace_file('/tmp/waf.json', waf_text)

            conf_path = Path(__file__).parent / './conf/http.conf'
            conf_text = conf_path.read_text()

            status, log_lines = self.orch.nginx_replace_config(
                conf_text, conf_path.name)
            self.assertEqual(0, status, log_lines)

            TestSecAddresses.config_setup_done = True

        # Consume any previous logging from the agent.
        self.orch.sync_service('agent')

    def do_request_query(self, query):
        status, _, _ = self.orch.send_nginx_http_request('/http?' + query, 80)
        self.assertEqual(status, 200)
        return self.do_request_common()

    def do_request_headers(self, headers):
        status, _, _ = self.orch.send_nginx_http_request('/http', 80, headers)
        self.assertEqual(status, 200)
        return self.do_request_common()


    def do_request_common(self):
        self.orch.reload_nginx()
        log_lines = self.orch.sync_service('agent')
        entries = [json.loads(line) for line in log_lines]
        # find _dd.appsec.json in one of the spans of the traces
        for entry in entries:
            for trace in entry:
                for span in trace:
                    if span.get('meta', {}).get('_dd.appsec.json'):
                        return json.loads(span['meta']['_dd.appsec.json'])
        self.failureException('No _dd.appsec.json found in traces')

    def test_key(self):
        result = self.do_request('a=&matched+key=')
        self.assertEqual(result['triggers'][0]['rule_matches'][0]['parameters'][0]['key_path'], ['matched key'])

    def test_key_percent_enc(self):
        result = self.do_request('a=&matched%20key=')
        self.assertEqual(result['triggers'][0]['rule_matches'][0]['parameters'][0]['key_path'], ['matched key'])

    def test_key_unfinished_percent_enc(self):
        result = self.do_request('matched+key%')
        self.assertEqual(result['triggers'][0]['rule_matches'][0]['parameters'][0]['key_path'], ['matched key%'])

    def test_key_unfinished_percent_enc2(self):
        result = self.do_request('matched+key%a')
        self.assertEqual(result['triggers'][0]['rule_matches'][0]['parameters'][0]['key_path'], ['matched key%a'])

    def test_key_unfinished_percent_enc3(self):
        result = self.do_request('%matched+key')
        self.assertEqual(result['triggers'][0]['rule_matches'][0]['parameters'][0]['key_path'], ['%matched key'])

    def test_key_unfinished_percent_enc4(self):
        result = self.do_request('%amatched+key')
        self.assertEqual(result['triggers'][0]['rule_matches'][0]['parameters'][0]['key_path'], ['%amatched key'])

    def test_value(self):
        result = self.do_request('key=matched+value')
        self.assertEqual(result['triggers'][0]['rule_matches'][0]['parameters'][0]['value'], 'matched value')

    def test_value_first_occurrence(self):
        result = self.do_request('key=matched+value&key=another+value')
        self.assertEqual(result['triggers'][0]['rule_matches'][0]['parameters'][0]['value'], 'matched value')

    def test_value_second_occurrence(self):
        result = self.do_request('key=another+value&key=matched+value')
        self.assertEqual(result['triggers'][0]['rule_matches'][0]['parameters'][0]['value'], 'matched value')

    def test_value_third_occurrence(self):
        result = self.do_request('key=another+value&key=yet+another+value&key=matched+value')
        self.assertEqual(result['triggers'][0]['rule_matches'][0]['parameters'][0]['value'], 'matched value')

    def test_value_percent_enc(self):
        result = self.do_request('key=matched%20value')
        self.assertEqual(result['triggers'][0]['rule_matches'][0]['parameters'][0]['value'], 'matched value')

    def test_value_unfinished_percent_enc(self):
        result = self.do_request('key=matched%20value%')
        self.assertEqual(result['triggers'][0]['rule_matches'][0]['parameters'][0]['value'], 'matched value%')

    def test_value_unfinished_percent_enc2(self):
        result = self.do_request('key=matched%20value%a')
        self.assertEqual(result['triggers'][0]['rule_matches'][0]['parameters'][0]['value'], 'matched value%a')

    def test_value_unfinished_percent_enc3(self):
        result = self.do_request('key=%matched%20value')
        self.assertEqual(result['triggers'][0]['rule_matches'][0]['parameters'][0]['value'], '%matched value')

    def test_value_unfinished_percent_enc4(self):
        result = self.do_request('key=%amatched%20value')
        self.assertEqual(result['triggers'][0]['rule_matches'][0]['parameters'][0]['value'], '%amatched value')

    def test_cookie_simple(self):
        result = self.do_request_headers({'Cookie': 'key=matched+value'})
        self.assertEqual(result['triggers'][0]['rule_matches'][0]['parameters'][0]['value'], 'matched value')