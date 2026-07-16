"""Regression test for request termination while an AppSec WAF task runs.

`PolTaskCtx::submit()` (src/security/context.cpp) increments
`req_.main->count` to keep the request alive while a WAF task runs on a
background thread, but historically failed to also increment
`req_.main->blocked`. nginx's `ngx_http_terminate_request()` resets
`r->main->count` and force-frees the request pool unless
`r->main->blocked` is nonzero. An error that terminates a request while the
task is queued or running could therefore destroy the pool while the task and
its completion handler still held pointers into it.

The test-only delay directive holds the task in flight, while the test-only
termination directive schedules `ngx_http_finalize_request(..., NGX_ERROR)`
on nginx's main thread. This deterministically exercises the same termination
branch used by real request errors without relying on socket-close timing.
"""

from pathlib import Path

from .. import case
from .. import orchestration


class TestTaskTeardownRace(case.TestCase):
    requires_waf = True
    min_nginx_version = '1.26.0'

    # How long (ms) to artificially hold each WAF task "in flight" on the
    # thread pool before its completion handler is allowed to run.
    TASK_DELAY_MS = 1000
    TERMINATION_DELAY_MS = 500

    INITIAL_WAF_TASK = 1 << 0
    REQUEST_BODY_WAF_TASK = 1 << 1
    FINAL_WAF_TASK = 1 << 2

    TERMINATION_MASKS = {
        'test_initial_waf_task_survives_request_termination': INITIAL_WAF_TASK,
        'test_request_body_waf_task_survives_request_termination':
        REQUEST_BODY_WAF_TASK,
        'test_final_waf_task_survives_request_termination': FINAL_WAF_TASK,
    }

    def setUp(self):
        super().setUp()
        waf_path = Path(__file__).parent / './conf/waf.json'
        waf_text = waf_path.read_text()
        self.orch.nginx_replace_file('/tmp/waf.json', waf_text)

        termination_mask = self.TERMINATION_MASKS[self._testMethodName]

        config = f"""
thread_pool waf_thread_pool threads=2 max_queue=5;

load_module /datadog-tests/ngx_http_datadog_module.so;

error_log stderr debug;

events {{
    worker_connections  1024;
}}

http {{
    datadog_agent_url http://agent:8126;
    datadog_appsec_enabled on;
    datadog_appsec_ruleset_file /tmp/waf.json;
    datadog_appsec_waf_timeout 2s;
    datadog_waf_thread_pool_name waf_thread_pool;
    datadog_appsec_max_saved_output_data 64k;
    datadog_appsec_test_task_delay_ms {self.TASK_DELAY_MS};
    datadog_appsec_test_task_termination_delay_ms {self.TERMINATION_DELAY_MS};
    datadog_appsec_test_task_termination_mask {termination_mask};

    client_max_body_size 10m;

    server {{
        listen              80;

        location /http {{
            proxy_pass http://http:8080;
        }}
    }}
}}
"""
        status, log_lines = self.orch.nginx_replace_config(
            config, 'task_teardown_race.conf')
        self.assertEqual(0, status, log_lines)

        # Drain any log lines accumulated so far so that
        # assert_no_worker_crash only observes what happens during this test.
        nginx_log_queue = self.orch.logs['nginx']
        while not nginx_log_queue.empty():
            try:
                nginx_log_queue.get_nowait()
            except Exception:
                break

    def assert_task_survives_request_termination(self,
                                                 request_text,
                                                 expect_empty_response=True):
        nginx_container = self.orch.containers['nginx']
        worker_pids_before = orchestration.nginx_worker_pids(
            nginx_container, self.orch.verbose)
        self.assertTrue(worker_pids_before,
                        "expected to find at least one nginx worker process")

        # The hooks terminate this request on nginx's main thread while the
        # selected WAF task remains delayed on the thread pool. The client
        # waits until nginx finishes the termination and closes the socket.
        response = self.orch.send_nginx_request_until_close(request_text,
                                                            port=80)
        if expect_empty_response:
            self.assertEqual('', response)

        # Poll through the delayed task's completion. Without blocked, nginx
        # frees the request before this point and the worker uses freed state.
        log_lines = self.assert_no_worker_crash(
            timeout_secs=(self.TASK_DELAY_MS / 1000.0) + 2)

        self.assertTrue(
            any('request was terminated while task' in line
                and 'running the connection write handler' in line
                for line in log_lines),
            'the WAF task did not take the safe asynchronous request '
            'termination completion path')

        # The nginx worker(s) present before the race should still be
        # running (i.e. nginx did not crash and get respawned by the
        # master process).
        worker_pids_after = orchestration.nginx_worker_pids(
            nginx_container, self.orch.verbose)
        self.assertEqual(
            worker_pids_before, worker_pids_after,
            "nginx worker process(es) changed, indicating a crash/respawn")

    def test_initial_waf_task_survives_request_termination(self):
        request_text = ("GET /http HTTP/1.1\r\n"
                        "Host: nginx\r\n"
                        "Connection: close\r\n"
                        "\r\n")
        self.assert_task_survives_request_termination(request_text)

    def test_request_body_waf_task_survives_request_termination(self):
        body = '{"test":"safe"}'
        request_text = ("POST /http HTTP/1.1\r\n"
                        "Host: nginx\r\n"
                        "Content-Type: application/json\r\n"
                        f"Content-Length: {len(body)}\r\n"
                        "Connection: close\r\n"
                        "\r\n"
                        f"{body}")
        self.assert_task_survives_request_termination(request_text)

    def test_final_waf_task_survives_request_termination(self):
        request_text = (
            "GET /http/response_body_test?trigger=safe&format=json HTTP/1.1\r\n"
            "Host: nginx\r\n"
            "Connection: close\r\n"
            "\r\n")
        self.assert_task_survives_request_termination(
            request_text, expect_empty_response=False)
