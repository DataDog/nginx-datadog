import time
from pathlib import Path
from .. import case, formats


class TestApiSecuritySampling(case.TestCase):
    requires_waf = True

    def setUp(self):
        super().setUp()
        # Always reload WAF file to ensure we have the latest version
        waf_path = Path(__file__).parent / './conf/waf.json'
        waf_text = waf_path.read_text()
        self.orch.nginx_replace_file('/tmp/waf.json', waf_text)
        # Consume any previous logging from the agent.
        self.orch.sync_service('agent')

    def do_request_and_get_spans(self, path='/http', method='GET', headers=None, req_body=None):
        """Make a request and return all spans from the trace"""
        if headers is None:
            headers = {}
        
        status, _, _ = self.orch.send_nginx_http_request(
            path, 80, headers=headers, method=method, req_body=req_body)
        self.assertEqual(status, 200)
        
        self.orch.reload_nginx()
        log_lines = self.orch.sync_service("agent")
        entries = [
            entry for entry in (formats.parse_trace(line)
                                for line in log_lines) if entry is not None
        ]
        
        # Collect all spans from all traces
        spans = []
        for entry in entries:
            for trace in entry:
                for span in trace:
                    spans.append(span)
        return spans

    def find_api_security_schemas(self, spans):
        """Find all API security schema tags starting with _dd.appsec.s."""
        schema_tags = {}
        for span in spans:
            meta = span.get('meta', {})
            for key, value in meta.items():
                if key.startswith('_dd.appsec.s.'):
                    schema_tags[key] = value
        return schema_tags

    def test_sampling_rate_limit(self):
        """Test that sampling respects rate limit of 2 requests/min"""
        # Configure API security with 2 requests per minute rate limit
        conf_path = Path(__file__).parent / './conf/http_sample_rate_2.conf'
        conf_text = conf_path.read_text()
        status, log_lines = self.orch.nginx_replace_config(conf_text, conf_path.name)
        self.assertEqual(0, status, log_lines)

        # Make 5 requests WITHOUT reloading nginx between them
        # This ensures the rate limiter state persists across requests
        requests_with_schemas = 0
        all_spans = []
        
        for i in range(5):
            # Make request without reloading nginx
            status, _, _ = self.orch.send_nginx_http_request(
                f'/http?request={i}', 80, headers={}, method='GET', req_body=None)
            self.assertEqual(status, 200)
            
            time.sleep(0.1)  # Small delay between requests

        # Now reload nginx once and get all the spans
        self.orch.reload_nginx()
        log_lines = self.orch.sync_service("agent")
        entries = [
            entry for entry in (formats.parse_trace(line)
                                for line in log_lines) if entry is not None
        ]
        
        # Collect all spans from all traces
        for entry in entries:
            for trace in entry:
                for span in trace:
                    all_spans.append(span)

        # Count requests that have schemas by grouping spans by request
        request_spans = {}
        for span in all_spans:
            meta = span.get('meta', {})
            # Try to identify which request this span belongs to
            for key, value in meta.items():
                if key.startswith('_dd.appsec.s.'):
                    # Extract request identifier from span if possible
                    request_id = "unknown"
                    if 'http.url' in meta:
                        url = meta['http.url']
                        if 'request=' in url:
                            request_id = url.split('request=')[1].split('&')[0]
                    
                    if request_id not in request_spans:
                        request_spans[request_id] = {}
                    request_spans[request_id][key] = value

        requests_with_schemas = len(request_spans)
        print(f"Found schemas in {requests_with_schemas} requests: {list(request_spans.keys())}")
        
        # Verify that exactly 2 requests have schemas (rate limit of 2 requests/min)
        self.assertEqual(requests_with_schemas, 2, 
                        f"Expected exactly 2 requests with schemas, but got {requests_with_schemas}")
