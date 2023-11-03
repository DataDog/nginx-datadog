from .. import case

from pathlib import Path


class TestAddHeader(case.TestCase):
    def run_test(self, conf_file):
        conf_path = Path(__file__).parent / 'conf' / conf_file
        status, lines = self.orch.nginx_replace_config(conf_path.read_text(),
                                                       conf_path.name)
        self.assertEqual(0, status, lines)

        # Send a request without requesting sampling delegation, and verify that
        # the `add_header` header is in the response.
        status, headers, body = self.orch.send_nginx_http_request('/http')
        self.assertEqual(200, status)
        self.assertEqual(1, sum(entry == ['X-I-Added-This-Header', 'for-real'] for entry in headers), headers)

        # Send a request that does request sampling delegation, and verify that
        # both the `add_header` header and the delegation header are in the
        # response.
        propagation_headers = {
            'X-Datadog-Delegate-Trace-Sampling': 'delegate',
            'X-Datadog-Trace-Id': 123,
            'X-Datadog-Parent-Id': 456
        }
        status, headers, body = self.orch.send_nginx_http_request('/http', headers=propagation_headers)
        self.assertEqual(200, status)
        self.assertEqual(1, sum(entry == ['X-I-Added-This-Header', 'for-real'] for entry in headers), headers)
        self.assertEqual(1, sum(key.lower() == 'x-datadog-trace-sampling-decision' for key, value in headers), headers)

        # Send a request without requesting sampling delegation, but to an
        # endpoint that performs delegation. Verify that the `add_header` header
        # is in the response, but that the delegation header is not.
        status, headers, body = self.orch.send_nginx_http_request('/delegate')
        self.assertEqual(200, status)
        # The `add_header` header will appear twice, because the nginx instance
        # calls itself.
        self.assertEqual(2, sum(entry == ['X-I-Added-This-Header', 'for-real'] for entry in headers), headers)
        self.assertEqual(0, sum(key.lower() == 'x-datadog-trace-sampling-decision' for key, value in headers), headers)

    def test_in_http(self):
        return self.run_test('add_header_in_http.conf')

    def test_in_server(self):
        return self.run_test('add_header_in_server.conf')

    def test_in_location(self):
        return self.run_test('add_header_in_location.conf')

    def test_in_http_server_location(self):
        return self.run_test('add_header_in_http_server_location.conf')

    def test_in_http_server(self):
        return self.run_test('add_header_in_http_server.conf')

    def test_in_http_location(self):
        return self.run_test('add_header_in_http_location.conf')

    def test_in_server_location(self):
        return self.run_test('add_header_in_server_location.conf')
