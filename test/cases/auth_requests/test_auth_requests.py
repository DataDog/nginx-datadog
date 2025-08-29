from .. import case
from .. import formats

import json
from pathlib import Path


class TestAuthRequests(case.TestCase):

    def test_no_auth_request_is_successful(self):
        conf_path = Path(__file__).parent / "./conf/http.conf"
        conf_text = conf_path.read_text()
        status, log_lines = self.orch.nginx_replace_config(
            conf_text, conf_path.name)
        self.assertEqual(status, 0, log_lines)

        self.orch.sync_service('agent')

        path = '/http-no-auth'
        status, _, body = self.orch.send_nginx_http_request(path)
        self.assertEqual(status, 200)
        response = json.loads(body)
        self.assertEqual(response["service"], "http")

        headers = response["headers"]
        trace_id = headers["x-datadog-trace-id"]
        span_id = headers["x-datadog-parent-id"]

        self.orch.reload_nginx()
        log_lines = self.orch.sync_service('agent')

        # Expect that nginx sent a trace containing two spans: one for the
        # request, and another for the auth subrequest.
        nginx_sent_a_trace = False
        for line in log_lines:
            trace = formats.parse_trace(line)
            if trace is None:
                # not a trace
                continue
            for chunk in trace:
                first, *rest = chunk
                if first['service'] == 'nginx':
                    nginx_sent_a_trace = True
                    self.assertEqual(0, len(rest), chunk)
                    self.assertEqual(first['meta']['http.url'], f'http://nginx{path}', first)
                    self.assertEqual(first['meta']['nginx.location'], path, first)

                    self.assertEqual(str(first['trace_id']), str(trace_id), trace)
                    self.assertEqual(str(first['span_id']), str(span_id), trace)

        self.assertTrue(nginx_sent_a_trace, log_lines)

    def test_auth_request_with_auth_token_is_successful(self):
        conf_path = Path(__file__).parent / "./conf/http.conf"
        conf_text = conf_path.read_text()
        status, log_lines = self.orch.nginx_replace_config(
            conf_text, conf_path.name)
        self.assertEqual(status, 0, log_lines)

        self.orch.sync_service('agent')

        status, _, body = self.orch.send_nginx_http_request(
            '/http', 80, headers={'x-token': 'mysecret'})
        self.assertEqual(status, 200)
        response = json.loads(body)
        self.assertEqual(response["service"], "http")

        headers = response["headers"]
        trace_id = headers["x-datadog-trace-id"]
        span_id = headers["x-datadog-parent-id"]

        self.orch.reload_nginx()
        log_lines = self.orch.sync_service('agent')

        # Expect that nginx sent a trace containing two spans: one for the
        # request, and another for the auth subrequest.
        nginx_sent_a_trace = False
        for line in log_lines:
            trace = formats.parse_trace(line)
            if trace is None:
                # not a trace
                continue
            for chunk in trace:
                first, *rest = chunk
                if first['service'] == 'nginx':
                    nginx_sent_a_trace = True
                    self.assertEqual(1, len(rest), chunk)
                    self.assertEqual(first['meta']['http.url'], 'http://nginx/http', first)
                    self.assertEqual(first['meta']['nginx.location'], '/http', first)

                    # Assert that the subrequest was traced
                    self.assertEqual('nginx', rest[0]['service'], rest[0])
                    self.assertEqual(rest[0]['meta']['http.url'], 'http://nginx/http', rest[0])
                    self.assertEqual(rest[0]['meta']['nginx.location'], '/auth', rest[0])

                    # Assert existing behavior that the trace ID and span ID
                    # match the trace ID and span ID of the nginx subrequest.
                    self.assertEqual(str(rest[0]['trace_id']), str(trace_id), trace)
                    self.assertEqual(str(rest[0]['span_id']), str(span_id), trace)

        self.assertTrue(nginx_sent_a_trace, log_lines)

    def test_auth_request_without_auth_token_is_not_successful(self):
        conf_path = Path(__file__).parent / "./conf/http.conf"
        conf_text = conf_path.read_text()
        status, log_lines = self.orch.nginx_replace_config(
            conf_text, conf_path.name)
        self.assertEqual(status, 0, log_lines)

        self.orch.sync_service('agent')

        status, _, _ = self.orch.send_nginx_http_request('/http')
        self.assertEqual(status, 401)

        self.orch.reload_nginx()
        log_lines = self.orch.sync_service('agent')

        # Expect that nginx sent a trace containing two spans: one for the
        # request, and another for the auth subrequest.
        nginx_sent_a_trace = False
        for line in log_lines:
            trace = formats.parse_trace(line)
            if trace is None:
                # not a trace
                continue
            for chunk in trace:
                first, *rest = chunk
                if first['service'] == 'nginx':
                    nginx_sent_a_trace = True
                    self.assertEqual(1, len(rest), chunk)
                    self.assertEqual(first['meta']['http.url'], 'http://nginx/http', first)
                    self.assertEqual(first['meta']['nginx.location'], '/http', first)

                    self.assertEqual('nginx', rest[0]['service'], rest[0])
                    self.assertEqual(rest[0]['meta']['http.url'], 'http://nginx/http', rest[0])
                    self.assertEqual(rest[0]['meta']['nginx.location'], '/auth', rest[0])

        self.assertTrue(nginx_sent_a_trace, log_lines)
