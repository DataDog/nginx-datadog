import queue
import re
import subprocess
import time

from .. import case
from .. import orchestration

from pathlib import Path


class TestSecBodyNoContentType(case.TestCase):
    requires_waf = True
    min_nginx_version = '1.26.0'

    def setUp(self):
        super().setUp()
        waf_path = Path(__file__).parent / './conf/waf.json'
        waf_text = waf_path.read_text()
        self.orch.nginx_replace_file('/tmp/waf.json', waf_text)

        conf_path = Path(__file__).parent / './conf/http_body_no_ct.conf'
        conf_text = conf_path.read_text()

        status, log_lines = self.orch.nginx_replace_config(
            conf_text, conf_path.name)
        self.assertEqual(0, status, log_lines)

        # Consume any previous logging from the agent.
        self.orch.sync_service('agent')

    def assert_no_worker_crash(self):
        """Fail if nginx logged a worker crash after the test request.

        parse_body_req runs in the WAF thread pool, so a SIGSEGV there kills
        the worker asynchronously.  The nginx master logs a line like:
          "worker process <pid> exited on signal 11"
        Scanning that log is more reliable than checking HTTP status codes,
        because the master respawns a worker that can still answer the
        follow-up request with 200.
        """
        crash_re = re.compile(r'exited on signal \d+')
        q = self.orch.logs['nginx']
        crash_lines = []
        while True:
            try:
                line = q.get_nowait()
            except queue.Empty:
                break
            if crash_re.search(line):
                crash_lines.append(line.strip())
        self.assertEqual([], crash_lines,
                         f'nginx worker crashed: {crash_lines}')

    def test_post_without_content_type_does_not_crash(self):
        """Regression for commit 64a952e: parse_body_req dereferenced a null
        content_type pointer via ngx_log_debug1 when built with --with-debug.

        Only meaningful when the module is built with CMAKE_BUILD_TYPE=Debug
        (which passes --with-debug to nginx's configure, enabling NGX_DEBUG and
        the ngx_log_debug* macros).
        """
        # Detect a Debug build by looking for a string that is only compiled
        # into the module when NGX_DEBUG is defined (it lives inside a
        # ngx_log_debug* call that expands to nothing in non-debug builds).
        result = subprocess.run(
            orchestration.docker_compose_command(
                "exec", "-T", "--", "nginx", "grep", "-qc",
                "failed to parse request body for WAF",
                "/datadog-tests/ngx_http_datadog_module.so"),
            capture_output=True,
            env=orchestration.child_env(),
        )
        if result.returncode != 0:
            self.skipTest(
                "module not built with NGX_DEBUG (CMAKE_BUILD_TYPE=Debug required)"
            )
        # Send a POST with a body but explicitly no Content-Type header.
        try:
            status, _, _ = self.orch.send_nginx_http_request(
                '/http',
                80,
                headers={"Content-Type": ""},
                req_body='key=value',
                method='POST')
        except subprocess.CalledProcessError:
            status = 0

        self.assertGreaterEqual(
            status, 100,
            f"no valid HTTP response (got {status}); nginx may have crashed")
        self.assertLess(status, 500,
                        f"unexpected server error status {status}")

        # Give the WAF thread a moment to process the body (and crash, without
        # the fix) before inspecting nginx's logs.
        time.sleep(2)

        # The authoritative crash check: no worker may have died on a signal.
        # parse_body_req runs in the WAF thread, so the crash is async: the
        # master respawns a worker that can still serve the follow-up request.
        self.assert_no_worker_crash()

        # Follow-up request confirms nginx is still alive.
        status2, _, _ = self.orch.send_nginx_http_request('/http', 80)
        self.assertGreaterEqual(
            status2, 100,
            f"nginx unresponsive after POST (got {status2}); may have crashed")
        self.assertLess(status2, 500,
                        f"nginx unresponsive after POST, status {status2}")
