import json
from pathlib import Path

from .. import case


class TestTaskPostFailure(case.TestCase):
    requires_waf = True
    min_nginx_version = '1.26.0'

    def setUp(self):
        super().setUp()
        # Set up WAF configuration - reuse from sec_blocking tests
        waf_path = Path(__file__).parent / './conf/waf.json'
        waf_text = waf_path.read_text()
        self.orch.nginx_replace_file('/tmp/waf.json', waf_text)

        crt_path = Path(__file__).parent / './cert/example.com.crt'
        crt_text = crt_path.read_text()
        self.orch.nginx_replace_file('/tmp/example.com.crt', crt_text)

        key_path = Path(__file__).parent / './cert/example.com.key'
        key_text = key_path.read_text()
        self.orch.nginx_replace_file('/tmp/example.com.key', key_text)

        # Consume any previous logging from the agent
        self.orch.sync_service('agent')

    def base_config_template(self, failure_mask=None):
        """Generate base nginx configuration with optional task post failure mask."""
        failure_line = ""
        if failure_mask is not None:
            failure_line = f"    datadog_appsec_test_task_post_failure_mask {failure_mask};"

        return f"""
thread_pool waf_thread_pool threads=2 max_queue=5;

load_module /datadog-tests/ngx_http_datadog_module.so;

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
{failure_line}

    client_max_body_size 10m;

    server {{
        listen              80;
        listen              443 quic reuseport;
        listen              443 ssl;
        http2               on;
        ssl_certificate     /tmp/example.com.crt;
        ssl_certificate_key /tmp/example.com.key;

        location /http {{
            proxy_pass http://http:8080;
        }}
    }}
}}
"""

    def run_request_with_failure(self, failure_mask, *req_args, **req_kwargs):
        config_with_failure = self.base_config_template(failure_mask)
        status, log_lines = self.orch.nginx_replace_config(
            config_with_failure, 'task_post_failure.conf')
        self.assertEqual(0, status, log_lines)

        status, resp_headers, body = self.orch.send_nginx_http_request(
            *req_args, **req_kwargs)

        self.orch.reload_nginx()
        log_lines = self.orch.sync_service('agent')

        # With task post failure, should get 200/410 (no blocking)
        if status != 200 and status != 410:
            self.fail(
                f"Expected 200 or 410 with failure mask {failure_mask}, got {status}"
            )

        # Should not have appsec.blocked tag since WAF didn't run
        traces = [
            json.loads(line) for line in log_lines if line.startswith('[[{')
        ]
        blocked_traces = [
            trace for trace in traces
            if any(span[0]['meta'].get('appsec.blocked') == 'true'
                   for span in trace)
        ]
        self.assertEqual(
            len(blocked_traces), 0,
            f"Expected no blocked traces with failure mask {failure_mask}")

        return status, resp_headers, body, log_lines

    def run_request_without_failure(self, expected_status, *req_args,
                                    **req_kwargs):
        # Test without failure mask to verify blocking would occur
        config_normal = self.base_config_template()
        status, log_lines = self.orch.nginx_replace_config(
            config_normal, 'normal.conf')
        self.assertEqual(0, status, log_lines)

        status, resp_headers, body = self.orch.send_nginx_http_request(
            *req_args, **req_kwargs)

        self.orch.reload_nginx()
        log_lines = self.orch.sync_service('agent')

        # Without failure mask, should get blocked
        self.assertEqual(
            expected_status, status,
            f"Expected blocking ({expected_status}) without failure mask, got {status}"
        )

        return status, resp_headers, body, log_lines

    def test_initial_waf_task_post_failure(self):
        """Test that initial WAF task post failure prevents blocking."""
        headers = {'User-Agent': 'block_json', 'Accept': '*/*'}
        # Test with failure mask (bit 0 = 1)
        self.run_request_with_failure(1, '/http', 80, headers=headers)

        # Verify that without failure mask, blocking would occur
        self.run_request_without_failure(403, '/http', 80, headers=headers)

    def test_request_body_waf_task_post_failure(self):
        """Test that request body WAF task post failure prevents blocking."""
        # Test with failure mask (bit 1 = 2)
        headers = {'Content-type': 'application/json'}
        req_body = '{"test": "block_default"}'
        self.run_request_with_failure(2,
                                      '/http',
                                      80,
                                      headers=headers,
                                      req_body=req_body)

        # Verify that without failure mask, blocking would occur
        self.run_request_without_failure(403,
                                         '/http',
                                         80,
                                         headers=headers,
                                         req_body=req_body)

    def test_final_waf_task_post_failure_head_request(self):
        """Test that final WAF task post failure prevents blocking on HEAD request."""
        # Test with failure mask (bit 2 = 4)
        self.run_request_with_failure(4, '/http/status/410', 80, method="HEAD")

        # Verify that without failure mask, blocking would occur
        self.run_request_without_failure(501,
                                         '/http/status/410',
                                         method="HEAD")

    def test_final_waf_task_post_failure_head_request_unparseable_body(self):
        """Test that final WAF task post failure prevents blocking on HEAD request (early final WAF run variant)."""
        # Verify that without failure mask, blocking would occur
        self.run_request_without_failure(
            501,
            '/http/response_body_test?trigger=safe&status=410&format=html',
            method="HEAD")
        # Test with failure mask (bit 2 = 4)
        self.run_request_with_failure(
            4,
            '/http/response_body_test?trigger=safe&status=410&format=html',
            80,
            method="HEAD")

    def test_final_waf_task_post_failure_non_parseable_content(self):
        """Test that final WAF task post failure prevents blocking on non-parseable response content
           (early waf submissions)."""
        # Test with failure mask (bit 2 = 4)
        # Use an endpoint that returns text/plain with blocking trigger
        self.run_request_with_failure(
            4, '/http/response_body_test?trigger=safe&format=html&status=410',
            80)

        # Verify that without failure mask, blocking would occur
        self.run_request_without_failure(
            501,
            '/http/response_body_test?trigger=safe&format=html&status=410', 80)

    def test_final_waf_task_post_failure_json_output(self):
        """Test that final WAF task post failure prevents blocking on JSON response content."""
        # Test with failure mask (bit 2 = 4)
        # Use an endpoint that returns JSON with blocking trigger
        self.run_request_with_failure(
            4, '/http/response_body_test?trigger=blo_res_bod&format=json', 80)

        # Verify that without failure mask, blocking would occur
        self.run_request_without_failure(
            501, '/http/response_body_test?trigger=blo_res_bod&format=json',
            80)
