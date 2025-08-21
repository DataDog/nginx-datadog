from pathlib import Path
from concurrent.futures import ThreadPoolExecutor, as_completed
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

    def test_sampling_rate_limit(self):
        """Test that sampling respects rate limit of 2 requests/min"""
        conf_path = Path(__file__).parent / './conf/http_sample_rate_2.conf'
        conf_text = conf_path.read_text()
        status, log_lines = self.orch.nginx_replace_config(
            conf_text, conf_path.name)
        self.assertEqual(0, status, log_lines)

        # make 5 requests concurrently
        def make_request(request_id):
            status, _, _ = self.orch.send_nginx_http_request(
                f'/http?request={request_id}',
                80,
                headers={},
                method='GET',
                req_body=None)
            return request_id, status

        with ThreadPoolExecutor(max_workers=5) as executor:
            futures = [executor.submit(make_request, i) for i in range(5)]
            for future in as_completed(futures):
                request_id, status = future.result()
                self.assertEqual(status, 200)

        # get all the spans
        self.orch.reload_nginx()
        log_lines = self.orch.sync_service("agent")
        entries = [
            entry for entry in (formats.parse_trace(line)
                                for line in log_lines) if entry is not None
        ]
        all_spans = []
        for entry in entries:
            for trace in entry:
                for span in trace:
                    all_spans.append(span)

        # collect schemas by requests
        request_schemas = {}
        for span in all_spans:
            meta = span.get('meta', {})
            for key, value in meta.items():
                if key.startswith('_dd.appsec.s.'):
                    request_id = "unknown"
                    if 'http.url' in meta:
                        url = meta['http.url']
                        if 'request=' in url:
                            request_id = url.split('request=')[1].split('&')[0]

                    if request_id not in request_schemas:
                        request_schemas[request_id] = {}
                    request_schemas[request_id][key] = value

        requests_with_schemas = len(request_schemas)
        print(
            f"Found schemas in {requests_with_schemas} requests: {list(request_schemas.keys())}"
        )

        self.assertEqual(
            requests_with_schemas, 2,
            f"Expected exactly 2 requests with schemas, but got {requests_with_schemas}"
        )
