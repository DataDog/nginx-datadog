from .. import case
from .. import formats

from pathlib import Path


class TestSampleRate(case.TestCase):

    def run_sample_rate_test(self,
                             conf_relative_path,
                             do_request,
                             expected_rate,
                             expected_line,
                             expected_dupe=1,
                             sync_nginx_logs=False):
        conf_path = Path(__file__).parent / conf_relative_path
        conf_text = conf_path.read_text()
        status, log_lines = self.orch.nginx_replace_config(
            conf_text, conf_path.name)
        self.assertEqual(0, status, log_lines)

        # Clear any outstanding logs from the agent.
        self.orch.sync_service('agent')

        # Send a request to nginx that will generate a trace.
        do_request()

        # Reload nginx to force it to send its traces.
        self.orch.reload_nginx()

        nginx_log_lines = None
        if sync_nginx_logs:
            # Get any logs from nginx, to eventually return to our caller.
            nginx_log_lines = self.orch.sync_service('nginx')

        agent_log_lines = self.orch.sync_service('agent')
        # Find the trace that came from nginx, and parse its trace chunks.
        chunks = []
        for line in agent_log_lines:
            trace = formats.parse_trace(line)
            if trace is None:
                # not a trace; some other logging
                continue
            for chunk in trace:
                if chunk[0]['service'] != 'nginx':
                    continue
                chunks.append(chunk)

        self.assertEqual(1, len(chunks), chunks)
        chunk = chunks[0]
        self.assertEqual(1, len(chunk), chunk)
        span = chunk[0]

        # The span will have a "nginx.sample_rate_source" tag referring to the
        # line in the configuration where the matching "datadog_sample_rate"
        # directive appears.
        # The full value of the tag is <path_to_nginx.conf>:<line>#<dupe>
        # e.g. "/etc/nginx/nginx.conf:12#1".
        # The "#1" is in case there are multiple directives on the same line.
        # "#1" is the first, "#2" the second, etc.
        source_location = span['meta'].get('nginx.sample_rate_source')
        self.assertIsNotNone(source_location)

        conf_path, where = source_location.split(':')
        line, dupe = where.split('#')
        line, dupe = int(line), int(dupe)

        self.assertEqual(expected_dupe, dupe)
        self.assertEqual(expected_line, line)

        self.assertEqual(expected_rate, span['metrics'].get('_dd.rule_psr'))

        return nginx_log_lines

    def send_to_nginx_http(self):
        status, _, _ = self.orch.send_nginx_http_request('/http')
        self.assertEqual(200, status)

    def test_in_http(self):
        self.run_sample_rate_test(conf_relative_path='./conf/where/http.conf',
                                  do_request=self.send_to_nginx_http,
                                  expected_rate=0.42,
                                  expected_line=12)

    def test_in_server(self):
        self.run_sample_rate_test(
            conf_relative_path='./conf/where/server.conf',
            do_request=self.send_to_nginx_http,
            expected_rate=0.42,
            expected_line=16)

    def test_in_location(self):
        self.run_sample_rate_test(
            conf_relative_path='./conf/where/location.conf',
            do_request=self.send_to_nginx_http,
            expected_rate=0.42,
            expected_line=17)

    def test_in_server_location(self):
        self.run_sample_rate_test(
            conf_relative_path='./conf/where/server-location.conf',
            do_request=self.send_to_nginx_http,
            expected_rate=0.42,
            expected_line=21)

    def test_in_server_location(self):
        self.run_sample_rate_test(
            conf_relative_path='./conf/where/http-location.conf',
            do_request=self.send_to_nginx_http,
            expected_rate=0.42,
            expected_line=21)

    def test_in_http_server(self):
        self.run_sample_rate_test(
            conf_relative_path='./conf/where/http-server.conf',
            do_request=self.send_to_nginx_http,
            expected_rate=0.42,
            expected_line=21)

    def test_in_http_server_location(self):
        self.run_sample_rate_test(
            conf_relative_path='./conf/where/http-server-location.conf',
            do_request=self.send_to_nginx_http,
            expected_rate=0.42,
            expected_line=26)

    def run_error_test(self, conf_relative_path, diagnostic_excerpt):
        conf_path = Path(__file__).parent / conf_relative_path
        conf_text = conf_path.read_text()
        status, log_lines = self.orch.nginx_replace_config(
            conf_text, conf_path.name)

        self.assertNotEqual(0, status, log_lines)
        self.assertTrue(any(diagnostic_excerpt in line for line in log_lines),
                        log_lines)

    def test_rate_is_literal(self):
        self.run_error_test(
            conf_relative_path='./conf/rate/literal.conf',
            diagnostic_excerpt=
            'Expected a real number between 0.0 and 1.0, but the provided argument is not a number.'
        )

    def test_rate_is_float(self):
        self.run_error_test(
            conf_relative_path='./conf/rate/float.conf',
            diagnostic_excerpt=
            'Expected a real number between 0.0 and 1.0, but the provided argument is not a number.'
        )

    def test_rate_in_range(self):
        self.run_error_test(
            conf_relative_path='./conf/rate/in-range.conf',
            diagnostic_excerpt=
            'Expected a real number between 0.0 and 1.0, but the provided argument is out of range.'
        )

    def test_first_wins(self):
        self.run_sample_rate_test(conf_relative_path='./conf/first-wins.conf',
                                  do_request=self.send_to_nginx_http,
                                  expected_rate=0.42,
                                  expected_line=13)

    def test_off(self):
        self.run_sample_rate_test(conf_relative_path='./conf/off.conf',
                                  do_request=self.send_to_nginx_http,
                                  expected_rate=1.0,
                                  expected_line=14)

    def test_on(self):
        self.run_sample_rate_test(conf_relative_path='./conf/on.conf',
                                  do_request=self.send_to_nginx_http,
                                  expected_rate=0.42,
                                  expected_line=11)

    def test_conditional_hierarchy(self):
        table = [(['on', 'on', 'on', 'on', 'on', 'on'], 0.31, 26),
                 (['on', 'on', 'on', 'on', 'off', 'on'], 0.32, 27),
                 (['on', 'on', 'on', 'on', 'off', 'off'], 0.21, 22),
                 (['on', 'on', 'off', 'on', 'off', 'off'], 0.22, 23),
                 (['on', 'on', 'off', 'off', 'off', 'off'], 0.11, 15),
                 (['off', 'on', 'off', 'off', 'off', 'off'], 0.12, 16)]

        for toggles, expected_rate, expected_line in table:
            # Each "on" or "off" in `toggles` is the value for a request header
            # that will determine the enabledness for a corresponding
            # `datadog_sample_rate` directive in the configuration file.
            headers = {
                'x-http1': toggles[0],
                'x-http2': toggles[1],
                'x-server1': toggles[2],
                'x-server2': toggles[3],
                'x-location1': toggles[4],
                'x-location2': toggles[5]
            }

            def send_with_headers():
                status, _, _ = self.orch.send_nginx_http_request(
                    '/http', headers=headers)
                self.assertEqual(200, status)

            self.run_sample_rate_test(
                conf_relative_path='./conf/conditional-hierarchy.conf',
                do_request=send_with_headers,
                expected_rate=expected_rate,
                expected_line=expected_line)

    def test_bogus_condition(self):
        nginx_log_lines = self.run_sample_rate_test(
            conf_relative_path='./conf/bogus.conf',
            do_request=self.send_to_nginx_http,
            expected_rate=1.0,
            expected_line=18,
            sync_nginx_logs=True)

        diagnostic_excerpt = 'Expected "on" or "off". Proceeding as if it were "off".'
        self.assertTrue(
            any(diagnostic_excerpt in line for line in nginx_log_lines),
            nginx_log_lines)

    def test_same_line(self):
        table = [(['on', 'on', 'on'], 0.1, 16, 1),
                 (['off', 'on', 'on'], 0.2, 16, 2),
                 (['off', 'off', 'on'], 0.3, 16, 3)]

        for toggles, expected_rate, expected_line, expected_dupe in table:
            # Each "on" or "off" in `toggles` is the value for a request header
            # that will determine the enabledness for a corresponding
            # `datadog_sample_rate` directive in the configuration file.
            headers = {
                'x-first': toggles[0],
                'x-second': toggles[1],
                'x-third': toggles[2]
            }

            def send_with_headers():
                status, _, _ = self.orch.send_nginx_http_request(
                    '/http', headers=headers)
                self.assertEqual(200, status)

            self.run_sample_rate_test(
                conf_relative_path='./conf/same-line.conf',
                do_request=send_with_headers,
                expected_rate=expected_rate,
                expected_line=expected_line,
                expected_dupe=expected_dupe)
