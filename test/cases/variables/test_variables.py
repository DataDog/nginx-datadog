from .. import case
from .. import formats

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
            log_trace_id, log_span_id, propagation, location = json.loads(
                line[len(prefix):])
            self.assertEqual(trace_id, log_trace_id, line)
            self.assertEqual(span_id, log_span_id, line)
            self.assertEqual(dict, type(propagation))
            self.assertEqual('/https?[0-9]*', location)

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

        # The service being reverse proxied by nginx returns a JSON response
        # containing the request headers.  By instructing nginx to add headers
        # whose values depend on the variables, we can extract the values of
        # the variables from the response.
        self.assertIn('x-datadog-test-thingy', headers)
        header_trace_id, header_span_id, propagation, location = json.loads(
            headers['x-datadog-test-thingy'])
        self.assertEqual(trace_id, header_trace_id)
        self.assertEqual(span_id, header_span_id)
        self.assertEqual(dict, type(propagation))
        self.assertEqual('/https?[0-9]*', location)

    def test_which_span_id_in_headers(self):
        """Verify that when `datadog_trace_locations` is `on`, the span
        referred to by the `$datadog_span_id` variable in an added request
        header is the location span, not the request span.

        - load the relevant nginx.conf
        - sync agent (to consume any old log lines)
        - nginx request /http, with headers indicating nginx is not the head
        - reload nginx (to flush traces)
        - sync agent
        - verify that the value of `$datadog_span_id` is _not_ that span whose
          parent is the fake root span that we injected into the request to
          nginx.
        """
        conf_path = Path(
            __file__).parent / './conf/which_span_id_in_headers.conf'
        conf_text = conf_path.read_text()

        status, log_lines = self.orch.nginx_replace_config(
            conf_text, conf_path.name)
        self.assertEqual(0, status, log_lines)

        self.orch.sync_service('agent')

        headers = {
            'X-Datadog-Trace-ID': 123,
            'X-Datadog-Parent-ID': 456,
            'X-Datadog-Sampling-Priority': 2  # manual keep
        }
        status, body = self.orch.send_nginx_http_request('/http',
                                                         headers=headers)
        self.assertEqual(200, status)
        response = json.loads(body)
        headers = response['headers']

        self.assertIn('x-datadog-test-thingy', headers)
        variable_span_id = int(json.loads(headers['x-datadog-test-thingy']))

        self.orch.reload_nginx()
        log_lines = self.orch.sync_service('agent')

        # Map {span ID -> parent ID} for each span sent to the agent by nginx.
        # This will allow us to check that $datadog_span_id is the _location_
        # span, because the parent of the location span will be the request
        # span, _not_ the span we injected into our request to nginx.
        parent_by_span_id = {}
        nginx_sent_a_trace = False
        for line in log_lines:
            trace = formats.parse_trace(line)
            if trace is None:
                # not a trace; some other logging
                continue
            for chunk in trace:
                first, *rest = chunk
                if first['service'] == 'nginx':
                    nginx_sent_a_trace = True
                    for span in chunk:
                        parent_by_span_id[span['span_id']] = span['parent_id']

        self.assertTrue(nginx_sent_a_trace)
        self.assertIn(variable_span_id, parent_by_span_id)

        variable_span_parent = parent_by_span_id[variable_span_id]
        self.assertNotEqual(variable_span_parent, 456)

    def test_which_span_id_in_access_log_format(self):
        """Verify that when `datadog_trace_locations` is `on`, the span
        referred to by the `$datadog_span_id` variable in an access log format
        string is the location span, not the request span.
        """
        conf_path = Path(
            __file__).parent / './conf/which_span_id_in_access_log_format.conf'
        conf_text = conf_path.read_text()

        status, log_lines = self.orch.nginx_replace_config(
            conf_text, conf_path.name)
        self.assertEqual(0, status, log_lines)

        self.orch.sync_service('agent')

        # Drain any old nginx log lines.
        self.orch.sync_nginx_access_log()

        headers = {
            'X-Datadog-Trace-ID': 123,
            'X-Datadog-Parent-ID': 456,
            'X-Datadog-Sampling-Priority': 2  # manual keep
        }
        status, body = self.orch.send_nginx_http_request('/http',
                                                         headers=headers)
        self.assertEqual(200, status, body)
        response = json.loads(body)
        headers = response['headers']

        log_lines = self.orch.sync_nginx_access_log()
        prefix = 'here is your span ID: '
        logged_span_id = None
        for line in log_lines:
            if not line.startswith(prefix):
                continue
            logged_span_id = int(line[len(prefix):])

        self.assertNotEqual(None, logged_span_id)

        # Now look at the agent's logs to see the spans actually sent, and
        # verify that `logged_span_id` is the _location_ span, not the request
        # span.
        self.orch.reload_nginx()
        log_lines = self.orch.sync_service('agent')

        parent_by_span_id = {}
        nginx_sent_a_trace = False
        for line in log_lines:
            trace = formats.parse_trace(line)
            if trace is None:
                # not a trace; some other logging
                continue
            for chunk in trace:
                first, *rest = chunk
                if first['service'] == 'nginx':
                    nginx_sent_a_trace = True
                    for span in chunk:
                        parent_by_span_id[span['span_id']] = span['parent_id']

        self.assertTrue(nginx_sent_a_trace)
        self.assertIn(logged_span_id, parent_by_span_id)

        logged_span_parent = parent_by_span_id[logged_span_id]
        self.assertNotEqual(logged_span_parent, 456)
