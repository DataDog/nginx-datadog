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

    def do_response_headers(self, endpoint):
        status, _, _ = self.orch.send_nginx_http_request(endpoint, 80)
        self.assertEqual(status, 200)
        return self.do_request_common()

    def do_response_code(self, endpoint, expected_code):
        status, _, _ = self.orch.send_nginx_http_request(endpoint, 80)
        self.assertEqual(status, expected_code)
        return self.do_request_common()

    def do_put(self, endpoint):
        self.orch.send_nginx_http_request(endpoint, 80, method='PUT')
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
        result = self.do_request_query('a=&matched+key=')
        self.assertEqual(
            result['triggers'][0]['rule_matches'][0]['parameters'][0]
            ['key_path'], ['matched key'])

    def test_key_percent_enc(self):
        result = self.do_request_query('a=&matched%20key=')
        self.assertEqual(
            result['triggers'][0]['rule_matches'][0]['parameters'][0]
            ['key_path'], ['matched key'])

    def test_key_unfinished_percent_enc(self):
        result = self.do_request_query('matched+key%')
        self.assertEqual(
            result['triggers'][0]['rule_matches'][0]['parameters'][0]
            ['key_path'], ['matched key%'])

    def test_key_unfinished_percent_enc2(self):
        result = self.do_request_query('matched+key%a')
        self.assertEqual(
            result['triggers'][0]['rule_matches'][0]['parameters'][0]
            ['key_path'], ['matched key%a'])

    def test_key_unfinished_percent_enc3(self):
        result = self.do_request_query('%matched+key')
        self.assertEqual(
            result['triggers'][0]['rule_matches'][0]['parameters'][0]
            ['key_path'], ['%matched key'])

    def test_key_unfinished_percent_enc4(self):
        result = self.do_request_query('%amatched+key')
        self.assertEqual(
            result['triggers'][0]['rule_matches'][0]['parameters'][0]
            ['key_path'], ['%amatched key'])

    def test_value(self):
        result = self.do_request_query('key=matched+value')
        self.assertEqual(
            result['triggers'][0]['rule_matches'][0]['parameters'][0]['value'],
            'matched value')

    def test_value_first_occurrence(self):
        result = self.do_request_query('key=matched+value&key=another+value')
        self.assertEqual(
            result['triggers'][0]['rule_matches'][0]['parameters'][0]['value'],
            'matched value')

    def test_value_second_occurrence(self):
        result = self.do_request_query('key=another+value&key=matched+value')
        self.assertEqual(
            result['triggers'][0]['rule_matches'][0]['parameters'][0]['value'],
            'matched value')

    def test_value_third_occurrence(self):
        result = self.do_request_query(
            'key=another+value&key=yet+another+value&key=matched+value')
        self.assertEqual(
            result['triggers'][0]['rule_matches'][0]['parameters'][0]['value'],
            'matched value')

    def test_value_percent_enc(self):
        result = self.do_request_query('key=matched%20value')
        self.assertEqual(
            result['triggers'][0]['rule_matches'][0]['parameters'][0]['value'],
            'matched value')

    def test_value_unfinished_percent_enc(self):
        result = self.do_request_query('key=matched%20value%')
        self.assertEqual(
            result['triggers'][0]['rule_matches'][0]['parameters'][0]['value'],
            'matched value%')

    def test_value_unfinished_percent_enc2(self):
        result = self.do_request_query('key=matched%20value%a')
        self.assertEqual(
            result['triggers'][0]['rule_matches'][0]['parameters'][0]['value'],
            'matched value%a')

    def test_value_unfinished_percent_enc3(self):
        result = self.do_request_query('key=%matched%20value')
        self.assertEqual(
            result['triggers'][0]['rule_matches'][0]['parameters'][0]['value'],
            '%matched value')

    def test_value_unfinished_percent_enc4(self):
        result = self.do_request_query('key=%amatched%20value')
        self.assertEqual(
            result['triggers'][0]['rule_matches'][0]['parameters'][0]['value'],
            '%amatched value')

    def test_cookie_simple(self):
        result = self.do_request_headers({'Cookie': 'key=matched+value'})
        self.assertEqual(
            result['triggers'][0]['rule_matches'][0]['parameters'][0]['value'],
            'matched value')

    def test_cookie_trimming(self):
        result = self.do_request_headers(
            {'Cookie': 'a=b; key=  matched+value '})
        self.assertEqual(
            result['triggers'][0]['rule_matches'][0]['parameters'][0]['value'],
            'matched value')

    def test_cookie_multiple(self):
        result = self.do_request_headers([('Cookie', 'a=b'),
                                          ('Cookie', 'key=matched+value')])
        self.assertEqual(
            result['triggers'][0]['rule_matches'][0]['parameters'][0]['value'],
            'matched value')

    def test_cookie_multiple2(self):
        result = self.do_request_headers([('Cookie', 'key=matched+value'),
                                          ('Cookie', 'a=b')])
        self.assertEqual(
            result['triggers'][0]['rule_matches'][0]['parameters'][0]['value'],
            'matched value')

    def test_cookie_key(self):
        result = self.do_request_headers({'Cookie': 'matched+key=value'})
        self.assertEqual(
            result['triggers'][0]['rule_matches'][0]['parameters'][0]
            ['key_path'], ['matched key'])

    def test_uri_raw(self):
        """The URI includes the query string and is not decoded"""
        result = self.do_request_query('a=the_matched+partial+value&')
        self.assertEqual(
            result['triggers'][0]['rule_matches'][0]['parameters'][0]['value'],
            '/http?a=the_matched+partial+value&')

    def test_header_key(self):
        result = self.do_request_headers({'MatcheD-key': 'value'})
        self.assertEqual(
            result['triggers'][0]['rule_matches'][0]['parameters'][0]
            ['key_path'], ['matched-key'])

    def test_header_value(self):
        result = self.do_request_headers({'key': 'matched value'})
        self.assertEqual(
            result['triggers'][0]['rule_matches'][0]['parameters'][0]['value'],
            'matched value')

    def test_header_multiple_values1(self):
        result = self.do_request_headers([('key', 'matched value'),
                                          ('key', 'another value')])
        self.assertEqual(
            result['triggers'][0]['rule_matches'][0]['parameters'][0]['value'],
            'matched value')

    def test_header_multiple_values2(self):
        result = self.do_request_headers([('key', 'another value'),
                                          ('key', 'matched value')])
        self.assertEqual(
            result['triggers'][0]['rule_matches'][0]['parameters'][0]['value'],
            'matched value')

    def test_resp_header_key(self):
        result = self.do_response_headers('/resp_header_key')
        self.assertEqual(
            result['triggers'][0]['rule_matches'][0]['parameters'][0]
            ['key_path'], ['matched-key'])

    def test_resp_header_value1(self):
        result = self.do_response_headers('/resp_header_value1')
        self.assertEqual(
            result['triggers'][0]['rule_matches'][0]['parameters'][0]['value'],
            'matched value')

    def test_resp_header_value2(self):
        result = self.do_response_headers('/resp_header_value2')
        self.assertEqual(
            result['triggers'][0]['rule_matches'][0]['parameters'][0]['value'],
            'matched value')

    def test_resp_header_value3(self):
        result = self.do_response_headers('/resp_header_value3')
        self.assertEqual(
            result['triggers'][0]['rule_matches'][0]['parameters'][0]['value'],
            'matched value')

    def test_500(self):
        result = self.do_response_code('/http/status/500', 500)
        self.assertEqual(
            result['triggers'][0]['rule_matches'][0]['parameters'][0]['value'],
            '500')

    def test_502(self):
        result = self.do_response_code('/http/status/502', 502)
        self.assertEqual(
            result['triggers'][0]['rule_matches'][0]['parameters'][0]['value'],
            '502')

    def test_request_method(self):
        result = self.do_put('/http')
        self.assertEqual(
            result['triggers'][0]['rule_matches'][0]['parameters'][0]['value'],
            'PUT')

    def test_client_ip_1(self):
        result = self.do_request_headers({'x-real-ip': '127.0.0.1, 1.2.3.4'})
        self.assertEqual(
            result['triggers'][0]['rule_matches'][0]['parameters'][0]['value'],
            '1.2.3.4')

    def test_client_ip_2(self):
        result = self.do_request_headers({'x-real-ip': '8.8.8.8, 1.2.3.4'})
        self.assertEqual(
            result['triggers'][0]['rule_matches'][0]['parameters'][0]['value'],
            '8.8.8.8')

    def test_client_ip_3(self):
        result = self.do_request_headers([('x-real-ip', '127.0.0.1'),
                                          ('x-real-ip', '1.2.3.4')])
        self.assertEqual(
            result['triggers'][0]['rule_matches'][0]['parameters'][0]['value'],
            '1.2.3.4')

    def test_client_ip_4(self):
        result = self.do_request_headers([('x-real-ip', '8.8.8.8'),
                                          ('x-real-ip', '1.2.3.4')])
        self.assertEqual(
            result['triggers'][0]['rule_matches'][0]['parameters'][0]['value'],
            '8.8.8.8')

    def test_client_ip_5(self):
        result = self.do_request_headers({'x-forwarded-for': '1.2.3.4'})
        self.assertEqual(
            result['triggers'][0]['rule_matches'][0]['parameters'][0]['value'],
            '1.2.3.4')

    def test_client_ip_6(self):
        result = self.do_request_headers({'true-client-ip': '1.2.3.4'})
        self.assertEqual(
            result['triggers'][0]['rule_matches'][0]['parameters'][0]['value'],
            '1.2.3.4')

    def test_client_ip_7(self):
        result = self.do_request_headers({'x-client-ip': '1.2.3.4'})
        self.assertEqual(
            result['triggers'][0]['rule_matches'][0]['parameters'][0]['value'],
            '1.2.3.4')

    def test_client_ip_8(self):
        result = self.do_request_headers(
            {'x-forwarded': 'for=127.0.0.1, for=1.2.3.4'})
        self.assertEqual(
            result['triggers'][0]['rule_matches'][0]['parameters'][0]['value'],
            '1.2.3.4')

    def test_client_ip_9(self):
        result = self.do_request_headers({'forwarded-for': '1.2.3.4'})
        self.assertEqual(
            result['triggers'][0]['rule_matches'][0]['parameters'][0]['value'],
            '1.2.3.4')

    def test_client_ip_10(self):
        result = self.do_request_headers({'x-cluster-client-ip': '1.2.3.4'})
        self.assertEqual(
            result['triggers'][0]['rule_matches'][0]['parameters'][0]['value'],
            '1.2.3.4')

    def test_client_ip_11(self):
        result = self.do_request_headers({'fastly-client-ip': '1.2.3.4'})
        self.assertEqual(
            result['triggers'][0]['rule_matches'][0]['parameters'][0]['value'],
            '1.2.3.4')

    def test_client_ip_12(self):
        result = self.do_request_headers({'cf-connecting-ip': '1.2.3.4'})
        self.assertEqual(
            result['triggers'][0]['rule_matches'][0]['parameters'][0]['value'],
            '1.2.3.4')

    def test_client_ip_13(self):
        result = self.do_request_headers({'cf-connecting-ipv6': 'fe80::1'})
        self.assertEqual(
            result['triggers'][0]['rule_matches'][0]['parameters'][0]['value'],
            'fe80::1')

    def test_client_ip_prio_1(self):
        result = self.do_request_headers({
            'x-real-ip': '8.8.8.8',
            'x-forwarded-for': '1.2.3.4'
        })
        self.assertEqual(
            result['triggers'][0]['rule_matches'][0]['parameters'][0]['value'],
            '1.2.3.4')

    def test_client_ip_prio_2(self):
        result = self.do_request_headers({
            'true-client-ip': '8.8.8.8',
            'x-real-ip': '1.2.3.4'
        })
        self.assertEqual(
            result['triggers'][0]['rule_matches'][0]['parameters'][0]['value'],
            '1.2.3.4')

    def test_client_ip_prio_3(self):
        result = self.do_request_headers({
            'x-client-ip': '8.8.8.8',
            'true-client-ip': '1.2.3.4'
        })
        self.assertEqual(
            result['triggers'][0]['rule_matches'][0]['parameters'][0]['value'],
            '1.2.3.4')

    def test_client_ip_prio_4(self):
        result = self.do_request_headers({
            'x-forwarded': 'for=8.8.8.8',
            'x-client-ip': '1.2.3.4'
        })
        self.assertEqual(
            result['triggers'][0]['rule_matches'][0]['parameters'][0]['value'],
            '1.2.3.4')

    def test_client_ip_prio_5(self):
        result = self.do_request_headers({
            'forwarded-for': '8.8.8.8',
            'x-forwarded': 'for=1.2.3.4'
        })
        self.assertEqual(
            result['triggers'][0]['rule_matches'][0]['parameters'][0]['value'],
            '1.2.3.4')

    def test_client_ip_prio_6(self):
        result = self.do_request_headers({
            'x-cluster-client-ip': '8.8.8.8',
            'forwarded-for': '1.2.3.4'
        })
        self.assertEqual(
            result['triggers'][0]['rule_matches'][0]['parameters'][0]['value'],
            '1.2.3.4')

    def test_client_ip_prio_7(self):
        result = self.do_request_headers({
            'fastly-client-ip': '8.8.8.8',
            'x-cluster-client-ip': '1.2.3.4'
        })
        self.assertEqual(
            result['triggers'][0]['rule_matches'][0]['parameters'][0]['value'],
            '1.2.3.4')

    def test_client_ip_prio_8(self):
        result = self.do_request_headers({
            'cf-connecting-ip': '8.8.8.8',
            'fastly-client-ip': '1.2.3.4'
        })
        self.assertEqual(
            result['triggers'][0]['rule_matches'][0]['parameters'][0]['value'],
            '1.2.3.4')

    def test_client_ip_prio_9(self):
        result = self.do_request_headers({
            'cf-connecting-ip': '1.2.3.4',
            'cf-connecting-ipv6': '1.2.3.4'
        })
        self.assertEqual(
            result['triggers'][0]['rule_matches'][0]['parameters'][0]['value'],
            '1.2.3.4')

    def test_obfuscation_default(self):
        result = self.do_request_query('password=matched+value')
        self.assertEqual(
            result['triggers'][0]['rule_matches'][0]['parameters'][0]['value'],
            '<Redacted>')
        self.assertEqual(
            result['triggers'][0]['rule_matches'][0]['parameters'][0]['highlight'][0],
            '<Redacted>')
