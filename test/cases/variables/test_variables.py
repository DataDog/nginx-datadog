from .. import case

import json
from pathlib import Path


class TestVariables(case.TestCase):
    def test_in_access_log_format(self):
        conf_path = Path(__file__).parent / './conf/in_access_log_format.conf'
        conf_text = conf_path.read_text()

        status, log_lines = self.orch.nginx_replace_config(
            conf_text, conf_path.name)
        self.assertEqual(0, status, log_lines)

        # Drain any old nginx log lines.
        self.orch.sync_nginx_access_log()

        status, body = self.orch.send_nginx_http_request('/http')
        self.assertEqual(200, status, body)
        response = json.loads(body)
        headers = response['headers']
        trace_id, span_id = int(headers['x-datadog-trace-id']), int(
            headers['x-datadog-parent-id'])

        log_lines = self.orch.sync_nginx_access_log()
        num_matching_lines = 0
        prefix = 'here is your access record: '
        for line in log_lines:
            if not line.startswith(prefix):
                continue
            num_matching_lines += 1
            log_trace_id, log_span_id, propagation = json.loads(
                line[len(prefix):])
            self.assertEqual(trace_id, log_trace_id, line)
            self.assertEqual(span_id, log_span_id, line)
            self.assertEqual(dict, type(propagation))

        self.assertEqual(1, num_matching_lines)

    def test_in_request_headers(self):
        conf_path = Path(__file__).parent / './conf/in_request_headers.conf'
        conf_text = conf_path.read_text()

        status, log_lines = self.orch.nginx_replace_config(
            conf_text, conf_path.name)
        self.assertEqual(0, status, log_lines)

        status, body = self.orch.send_nginx_http_request('/http')
        self.assertEqual(200, status)
        response = json.loads(body)
        headers = response['headers']
        trace_id, span_id = int(headers['x-datadog-trace-id']), int(
            headers['x-datadog-parent-id'])

        self.assertIn('x-datadog-test-thingy', headers)
        header_trace_id, header_span_id, propagation = json.loads(
            headers['x-datadog-test-thingy'])
        self.assertEqual(trace_id, header_trace_id)
        self.assertEqual(span_id, header_span_id)
        self.assertEqual(dict, type(propagation))
