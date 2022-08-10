from .. import case
from .. import formats

import json
from pathlib import Path
import re
import shlex


class TestAccessLog(case.TestCase):

    def test_default_format(self):
        """Verify that the default access log format contains the trace ID
        and span ID.
        """
        conf_path = Path(__file__).parent / './conf/default_format.conf'
        conf_text = conf_path.read_text()
        status, log_lines = self.orch.nginx_replace_config(
            conf_text, conf_path.name)
        self.assertEqual(0, status, log_lines)

        # discard any old log lines from nginx
        self.orch.sync_nginx_access_log()

        status, body = self.orch.send_nginx_http_request('/http')
        self.assertEqual(200, status)
        response = json.loads(body)
        trace_id = response['headers']['x-datadog-trace-id']
        span_id = response['headers']['x-datadog-parent-id']

        # Look through the logs for the access log message.
        log_lines = self.orch.sync_nginx_access_log()
        # From `log_conf.cpp`:
        #
        #    {"datadog_text", "escape=default",
        #    R"nginx($remote_addr - $http_x_forwarded_user [$time_local] "$request" $status $body_bytes_sent "$http_referer" "$http_user_agent" "$http_x_forwarded_for" "$datadog_trace_id" "$datadog_span_id")nginx"},
        #
        # $time_local is formatted as two parts, so [$time_local] is two shell lexemes.
        # If we use `shlex` to split the output, then the trace ID should be at
        # index 11, and the span ID at index 12.
        #
        # Assume that anything beginning with an IP address is an access log
        # line.
        found_it = False
        for line in log_lines:
            if re.match(r'[0-9]{1,3}(\.[0-9]{1,3}){3}\s', line) is None:
                continue
            lexemes = shlex.split(line)
            self.assertEqual(13, len(lexemes), lexemes)
            self.assertEqual(trace_id, lexemes[11], lexemes)
            self.assertEqual(span_id, lexemes[12], lexemes)
            found_it = True

        self.assertTrue(found_it, log_lines)

    def test_without_tracing(self):
        """Verify that the default access log format contains "-" placeholders
        for trace ID and span ID when tracing is disabled.
        """
        conf_path = Path(__file__).parent / './conf/without_tracing.conf'
        conf_text = conf_path.read_text()
        status, log_lines = self.orch.nginx_replace_config(
            conf_text, conf_path.name)
        self.assertEqual(0, status, log_lines)

        # discard any old log lines from nginx
        self.orch.sync_nginx_access_log()

        status, body = self.orch.send_nginx_http_request('/http')
        self.assertEqual(200, status)

        # Look through the logs for the access log message.
        log_lines = self.orch.sync_nginx_access_log()
        # From `log_conf.cpp`:
        #
        #    {"datadog_text", "escape=default",
        #    R"nginx($remote_addr - $http_x_forwarded_user [$time_local] "$request" $status $body_bytes_sent "$http_referer" "$http_user_agent" "$http_x_forwarded_for" "$datadog_trace_id" "$datadog_span_id")nginx"},
        #
        # $time_local is formatted as two parts, so [$time_local] is two shell lexemes.
        # If we use `shlex` to split the output, then the trace ID should be at
        # index 11, and the span ID at index 12.
        #
        # Assume that anything beginning with an IP address is an access log
        # line.
        found_it = False
        for line in log_lines:
            if re.match(r'[0-9]{1,3}(\.[0-9]{1,3}){3}\s', line) is None:
                continue
            lexemes = shlex.split(line)
            self.assertEqual(13, len(lexemes), lexemes)
            self.assertEqual('-', lexemes[11], lexemes)
            self.assertEqual('-', lexemes[12], lexemes)
            found_it = True

        self.assertTrue(found_it, log_lines)

    def test_json_format(self):
        """Verify that specifying the "datadog_json" access log format produces
        access log line containing a JSON object of relevant fields.
        """
        conf_path = Path(__file__).parent / './conf/json_format.conf'
        conf_text = conf_path.read_text()
        status, log_lines = self.orch.nginx_replace_config(
            conf_text, conf_path.name)
        self.assertEqual(0, status, log_lines)

        # discard any old log lines from nginx
        self.orch.sync_nginx_access_log()

        status, body = self.orch.send_nginx_http_request('/http')
        self.assertEqual(200, status)
        response = json.loads(body)
        trace_id = response['headers']['x-datadog-trace-id']
        span_id = response['headers']['x-datadog-parent-id']

        # Look through the logs for the access log message.
        log_lines = self.orch.sync_nginx_access_log()
        found_it = False
        for line in log_lines:
            try:
                record = json.loads(line)
            except json.decoder.JSONDecodeError:
                continue

            found_it = True
            self.assertEqual(dict, type(record))
            self.assertIn('trace_id', record)
            self.assertEqual(trace_id, record['trace_id'])
            self.assertIn('span_id', record)
            self.assertEqual(trace_id, record['span_id'])

        self.assertTrue(found_it)
