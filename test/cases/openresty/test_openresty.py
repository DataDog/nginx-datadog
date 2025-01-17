from .. import case
from .. import formats

from pathlib import Path
from unittest import skipUnless


@skipUnless(case.nginx_flavor() == case.NginxFlavor.OPENRESTY,
            "Not an OpenResty flavor")
class TestOpenResty(case.TestCase):

    def test_openresty_base(self):
        """Very that a basic openresty configuration works and can be traced
        - load the relevant nginx.conf
        - sync agent
        - openresty request /http
        - reload openresty  (to flush traces)
        - sync agent
        - verify there is only one span from openresty
        - verify openresty returns the right message
        """
        conf_path = Path(__file__).parent / "./conf/openresty_base.conf"
        conf_text = conf_path.read_text()
        status, log_lines = self.orch.nginx_replace_config(
            conf_text, conf_path.name)
        self.assertEqual(0, status, log_lines)

        self.orch.sync_service("agent")

        status, _, body = self.orch.send_nginx_http_request("/http")
        self.assertEqual(200, status)
        self.assertEqual(body, "openresty lua\n")

        self.orch.reload_nginx()
        log_lines = self.orch.sync_service("agent")

        # Since locations are not traced by default, we expect one trace from
        # the "nginx" service, and we expect it to contain one span.
        openresty_sent_a_trace = False
        for line in log_lines:
            trace = formats.parse_trace(line)
            if trace is None:
                # not a trace
                continue
            for chunk in trace:
                first, *rest = chunk
                if first["service"] == "nginx":
                    # Just one span from nginx.
                    openresty_sent_a_trace = True
                    self.assertEqual(0, len(rest), chunk)

        self.assertTrue(openresty_sent_a_trace, log_lines)
