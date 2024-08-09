from .. import case
from .. import formats

from pathlib import Path


class TestRewrite(case.TestCase):

    def test_rewrite(self):
        conf_path = Path(__file__).parent / "./conf/default.conf"
        conf_text = conf_path.read_text()
        status, log_lines = self.orch.nginx_replace_config(
            conf_text, conf_path.name)
        self.assertEqual(status, 0, log_lines)

        # Clear any outstanding logs from the agent.
        self.orch.sync_service("agent")

        status, _, body = self.orch.send_nginx_http_request("/http")
        self.assertEqual(status, 200)

        # Reload nginx to force it to send its traces.
        self.orch.reload_nginx()

        log_lines = self.orch.sync_service("agent")
        # Find the trace that came from nginx, and pass its chunks (groups of
        # spans) to the callback.
        found_nginx_trace = False
        for line in log_lines:
            trace = formats.parse_trace(line)
            if trace is None:
                # not a trace; some other logging
                continue
            for chunk in trace:
                if chunk[0]["service"] != "nginx":
                    continue
                found_nginx_trace = True

        self.assertTrue(found_nginx_trace)
