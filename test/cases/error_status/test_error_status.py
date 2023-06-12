from .. import case
from .. import formats

from pathlib import Path


class TestErrorStatus(case.TestCase):

    def send_request_with_expected_response_status(self, expected_status):
        """This function assumes that the nginx config is already "./conf/http.conf".

        It's faster to replace the config only once, this this function will be
        called in a loop.
        """
        self.orch.sync_service('agent')

        status, body = self.orch.send_nginx_http_request(
            f'/http/status/{expected_status}')
        self.assertEqual(expected_status, status)

        self.orch.reload_nginx()
        log_lines = self.orch.sync_service('agent')

        # Since locations are not traced by default, we expect one span from the
        # "nginx" service, and for the span to have `.error == 1`.
        nginx_sent_a_trace = False
        for line in log_lines:
            trace = formats.parse_trace(line)
            if trace is None:
                # not a trace
                continue
            for chunk in trace:
                first, *rest = chunk
                if first['service'] == 'nginx':
                    # Just one span from nginx.
                    nginx_sent_a_trace = True
                    self.assertEqual(0, len(rest), chunk)
                    self.assertEqual(first['error'], 1)

        self.assertTrue(nginx_sent_a_trace, log_lines)

    def test_5xx_status_yields_error_span(self):
        """Verify that a 5xx response produces an error span."""
        conf_path = Path(__file__).parent / './conf/http.conf'
        conf_text = conf_path.read_text()
        status, log_lines = self.orch.nginx_replace_config(
            conf_text, conf_path.name)
        self.assertEqual(0, status, log_lines)

        # For brevity, check only these:
        # <https://developer.mozilla.org/en-US/docs/Web/HTTP/Status#server_error_responses>
        for expected_status in range(500, 512):
            self.send_request_with_expected_response_status(expected_status)
