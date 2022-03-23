from .. import case
from .. import formats

from pathlib import Path


class TestLocationTracing(case.TestCase):
    def test_no_location_tracing_by_default(self):
        """Verify that `location` blocks are not traced by default.

        - load the relevant nginx.conf
        - sync agent (to consume any old log lines)
        - nginx request /http
        - reload nginx (to flush traces)
        - sync agent
        - verify there is only one span from nginx
        """
        conf_path = Path(__file__).parent / './conf/default.conf'
        conf_text = conf_path.read_text()
        status, log_lines = self.orch.nginx_replace_config(
            conf_text, conf_path.name)
        self.assertEqual(0, status, log_lines)

        self.orch.sync_service('agent')

        status, body = self.orch.send_nginx_http_request('/http')
        self.assertEqual(200, status)

        self.orch.reload_nginx()
        log_lines = self.orch.sync_service('agent')

        # Since locations are not traced by default, we expect one trace from
        # the "nginx" service, and we expect it to contain one span.
        nginx_sent_a_trace = False
        for line in log_lines:
            trace = formats.parse_trace(line)
            if trace is None:
                # not a trace
                continue
            for chunk in trace:
                root, *rest = chunk
                if root['service'] == 'nginx':
                    # Just one span from nginx.
                    nginx_sent_a_trace = True
                    self.assertEqual(0, len(rest), chunk)

        self.assertTrue(nginx_sent_a_trace, log_lines)

    def run_location_tracing_test(self, conf_relative_path):
        """Verify that the `datadog_trace_locations` directive causes an
        additional span to be produced per request (one for the request, and
        another for the `location`).

        - load the relevant nginx.conf
        - sync agent (to consume any old log lines)
        - nginx request /http
        - reload nginx (to flush traces)
        - sync agent
        - verify there are two spans from nginx
        """
        conf_path = Path(__file__).parent / conf_relative_path
        conf_text = conf_path.read_text()
        status, log_lines = self.orch.nginx_replace_config(
            conf_text, conf_path.name)
        self.assertEqual(0, status, log_lines)

        self.orch.sync_service('agent')

        status, body = self.orch.send_nginx_http_request('/http')
        self.assertEqual(200, status)

        self.orch.reload_nginx()
        log_lines = self.orch.sync_service('agent')

        # Expect that nginx sent a trace containing two spans: one for the
        # request, and another for the `location`.
        nginx_sent_a_trace = False
        for line in log_lines:
            trace = formats.parse_trace(line)
            if trace is None:
                # not a trace; some other logging
                continue
            for chunk in trace:
                root, *rest = chunk
                if root['service'] == 'nginx':
                    nginx_sent_a_trace = True
                    self.assertEqual(1, len(rest), chunk)
                    self.assertEqual('nginx', rest[0]['service'], rest[0])

        self.assertTrue(nginx_sent_a_trace)

    def test_trace_locations_in_http(self):
        return self.run_location_tracing_test(
            './conf/trace_locations_in_http.conf')

    def test_trace_locations_in_server(self):
        return self.run_location_tracing_test(
            './conf/trace_locations_in_server.conf')

    def test_trace_locations_in_location(self):
        return self.run_location_tracing_test(
            './conf/trace_locations_in_location.conf')
