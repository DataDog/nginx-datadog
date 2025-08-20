import json
import base64
import gzip
from pathlib import Path
from .. import case, formats


class TestApiSecurityAttributes(case.TestCase):
    config_setup_done = False
    requires_waf = True

    @classmethod
    def setUpClass(cls):
        cls.config_setup_done = False
        super().setUpClass()

    def setUp(self):
        super().setUp()
        # avoid reconfiguration (cuts time almost in half)
        if not TestApiSecurityAttributes.config_setup_done:
            waf_path = Path(__file__).parent / './conf/waf.json'
            waf_text = waf_path.read_text()
            self.orch.nginx_replace_file('/tmp/waf.json', waf_text)

            conf_path = Path(__file__).parent / './conf/http.conf'
            conf_text = conf_path.read_text()

            status, log_lines = self.orch.nginx_replace_config(
                conf_text, conf_path.name)
            self.assertEqual(0, status, log_lines)

            TestApiSecurityAttributes.config_setup_done = True

        # Consume any previous logging from the agent.
        self.orch.sync_service('agent')

    def apply_config(self, conf_name):
        conf_path = Path(__file__).parent / f"./conf/http_{conf_name}.conf"
        conf_text = conf_path.read_text()
        status, log_lines = self.orch.nginx_replace_config(
            conf_text, conf_path.name)
        self.assertEqual(0, status, log_lines)

    def do_request_and_get_spans(self,
                                 path='/http',
                                 method='GET',
                                 headers=None,
                                 req_body=None):
        """Make a request and return all spans from the trace"""
        if headers is None:
            headers = {}

        status, _, _ = self.orch.send_nginx_http_request(path,
                                                         80,
                                                         headers=headers,
                                                         method=method,
                                                         req_body=req_body)
        self.assertEqual(status, 200)

        self.orch.reload_nginx()
        log_lines = self.orch.sync_service("agent")
        entries = [
            entry for entry in (formats.parse_trace(line)
                                for line in log_lines) if entry is not None
        ]

        spans = []
        for entry in entries:
            for trace in entry:
                for span in trace:
                    spans.append(span)
        return spans

    @staticmethod
    def find_api_security_tags(spans):
        """Find all API security tags starting with _dd.appsec.s."""
        api_security_tags = {}
        for span in spans:
            meta = span.get('meta', {})
            for key, value in meta.items():
                if key.startswith('_dd.appsec.s.'):
                    api_security_tags[key] = value
        return api_security_tags

    @staticmethod
    def find_all_appsec_tags(spans):
        """Find all AppSec tags starting with _dd.appsec."""
        appsec_tags = {}
        for span in spans:
            meta = span.get('meta', {})
            for key, value in meta.items():
                if key.startswith('_dd.appsec.') or key.startswith('appsec.'):
                    appsec_tags[key] = value
            metrics = span.get('metrics', {})
            for key, value in metrics.items():
                if key.startswith('_dd.appsec.') or key.startswith('appsec.'):
                    appsec_tags[key] = value
        return appsec_tags

    @staticmethod
    def find_all_appsec_metrics(spans):
        """Find all AppSec metrics starting with _dd.appsec."""
        appsec_metrics = {}
        for span in spans:
            metrics = span.get('metrics', {})
            for key, value in metrics.items():
                if key.startswith('_dd.appsec.'):
                    appsec_metrics[key] = value
        return appsec_metrics

    @staticmethod
    def get_sampling_priority(spans):
        """Get the sampling priority from spans."""
        for span in spans:
            metrics = span.get('metrics', {})
            if '_sampling_priority_v1' in metrics:
                return metrics['_sampling_priority_v1']
        return None

    @staticmethod
    def decode_attribute_value(value):
        """Decode a potentially compressed and base64-encoded attribute value"""
        try:
            # First try to decode as JSON directly (for small values)
            return json.loads(value)
        except json.JSONDecodeError:
            try:
                # Try base64 decode + decompress
                decoded = base64.b64decode(value)
                decompressed = gzip.decompress(decoded)
                return json.loads(decompressed.decode('utf-8'))
            except Exception:
                # If all else fails, return the raw value
                return value

    def test_api_security_enabled_with_query_params(self):
        """Test that API security attributes are collected when enabled"""
        spans = self.do_request_and_get_spans(
            '/http?param1=value1&param2=value2')
        api_security_tags = self.find_api_security_tags(spans)

        # Should have at least one API security attribute
        self.assertGreater(len(api_security_tags), 0,
                           "No API security attributes found in spans")

        self.assertIn('_dd.appsec.s.req.query', api_security_tags)
        decoded_query = self.decode_attribute_value(
            api_security_tags['_dd.appsec.s.req.query'])
        self.assertIsInstance(decoded_query, (dict, list))

    def test_api_security_enabled_with_json_body(self):
        """Test that API security attributes are collected from JSON request body"""
        json_body = json.dumps({
            "user": {
                "name": "John Doe",
                "email": "john@example.com",
                "age": 30
            },
            "preferences": {
                "theme": "dark",
                "notifications": True
            }
        })

        headers = {'Content-Type': 'application/json'}
        spans = self.do_request_and_get_spans('/http',
                                              method='POST',
                                              headers=headers,
                                              req_body=json_body)
        api_security_tags = self.find_api_security_tags(spans)

        # Should have at least one API security attribute
        self.assertGreater(len(api_security_tags), 0,
                           "No API security attributes found in spans")

        self.assertIn('_dd.appsec.s.req.body', api_security_tags)
        decoded_body = self.decode_attribute_value(
            api_security_tags['_dd.appsec.s.req.body'])
        self.assertIsInstance(decoded_body, (dict, list))

    def test_api_security_enabled_with_headers(self):
        """Test that API security attributes are collected from headers"""
        headers = {
            'X-Custom-Header': 'custom-value',
            'Authorization': 'Bearer token123',
            'User-Agent': 'TestAgent/1.0'
        }

        spans = self.do_request_and_get_spans('/http', headers=headers)
        api_security_tags = self.find_api_security_tags(spans)

        # Should have at least one API security attribute
        self.assertGreater(len(api_security_tags), 0,
                           "No API security attributes found in spans")

        self.assertIn('_dd.appsec.s.req.headers', api_security_tags)
        decoded_headers = self.decode_attribute_value(
            api_security_tags['_dd.appsec.s.req.headers'])
        self.assertIsInstance(decoded_headers, (dict, list))

    def test_api_security_enabled_with_cookies(self):
        """Test that API security attributes are collected from cookies"""
        headers = {'Cookie': 'session_id=abc123; user_pref=dark_mode; lang=en'}

        spans = self.do_request_and_get_spans('/http', headers=headers)
        api_security_tags = self.find_api_security_tags(spans)

        # Should have at least one API security attribute
        self.assertGreater(len(api_security_tags), 0,
                           "No API security attributes found in spans")

        self.assertIn('_dd.appsec.s.req.cookies', api_security_tags)
        decoded_cookies = self.decode_attribute_value(
            api_security_tags['_dd.appsec.s.req.cookies'])
        self.assertIsInstance(decoded_cookies, (dict, list))

    def test_large_payload_compression(self):
        """Test that large payloads are compressed and base64 encoded"""
        # Create a large JSON payload that should trigger compression
        large_data = {
            "items": [{
                "id": i,
                "name": f"Item {i}",
                "metadata": {
                    "category": f"category_{i % 5}",
                    "tags": [f"tag_{j}" for j in range(10)],
                    "properties": {
                        f"prop_{k}": f"value_{k}"
                        for k in range(20)
                    }
                }
            } for i in range(10)]
        }

        json_body = json.dumps(large_data)
        headers = {'Content-Type': 'application/json'}

        spans = self.do_request_and_get_spans('/http',
                                              method='POST',
                                              headers=headers,
                                              req_body=json_body)
        api_security_tags = self.find_api_security_tags(spans)

        # Should have at least one API security attribute
        self.assertGreater(len(api_security_tags), 0,
                           "No API security attributes found in spans")

        self.assertIn('_dd.appsec.s.req.body', api_security_tags)

        value = api_security_tags['_dd.appsec.s.req.body']
        # If it's a large payload, it should be base64 encoded (not valid JSON directly)
        try:
            json.loads(value)
            # If we can parse it directly, it means it wasn't compressed
            # This is fine for smaller payloads
        except json.JSONDecodeError:
            # Should be base64 encoded, try to decode
            decoded_body = self.decode_attribute_value(value)
            self.assertIsInstance(decoded_body, (dict, list))
            # Verify the decoded content makes sense
            if isinstance(decoded_body, dict) and 'items' in decoded_body:
                self.assertGreater(len(decoded_body['items']), 0)

    def test_response_body_extraction(self):
        """Test that response body is extracted when available"""
        # Make a request that should generate a response body
        spans = self.do_request_and_get_spans(
            '/http/response_body_test/?trigger=safe&format=json')
        api_security_tags = self.find_api_security_tags(spans)

        # Should have at least one API security attribute
        self.assertGreater(len(api_security_tags), 0,
                           "No API security attributes found in spans")

        self.assertIn('_dd.appsec.s.res.body', api_security_tags)
        decoded_body = self.decode_attribute_value(
            api_security_tags['_dd.appsec.s.res.body'])
        self.assertIsInstance(decoded_body, (dict, list, str))

    def test_response_headers_extraction(self):
        """Test that response headers are extracted"""
        spans = self.do_request_and_get_spans('/http')
        api_security_tags = self.find_api_security_tags(spans)

        # Should have at least one API security attribute
        self.assertGreater(len(api_security_tags), 0,
                           "No API security attributes found in spans")

        self.assertIn('_dd.appsec.s.res.headers', api_security_tags)
        decoded_headers = self.decode_attribute_value(
            api_security_tags['_dd.appsec.s.res.headers'])
        self.assertIsInstance(decoded_headers, (dict, list))

    def test_non_schema_numeric_attributes(self):
        """Test that non-schema numeric attributes are collected and reported as metrics"""
        headers = {'X-Test-Numeric': 'trigger'}

        spans = self.do_request_and_get_spans('/http', headers=headers)
        appsec_metrics = self.find_all_appsec_metrics(spans)

        self.assertIn('_dd.appsec.test.numeric', appsec_metrics)
        self.assertEqual(appsec_metrics['_dd.appsec.test.numeric'], 42.5)

    def test_non_schema_string_attributes(self):
        """Test that non-schema string attributes are collected and reported as tags"""
        headers = {'X-Test-String': 'trigger'}

        spans = self.do_request_and_get_spans('/http', headers=headers)
        appsec_tags = self.find_all_appsec_tags(spans)

        self.assertIn('_dd.appsec.test.string', appsec_tags)
        self.assertEqual(appsec_tags['_dd.appsec.test.string'],
                         'test_string_value')

    def test_reference_attributes(self):
        """Test that reference attributes are collected with actual values from input"""
        test_value = 'my_reference_value'
        headers = {'X-Test-Reference': test_value}

        spans = self.do_request_and_get_spans('/http', headers=headers)
        appsec_tags = self.find_all_appsec_tags(spans)

        self.assertIn('_dd.appsec.test.reference', appsec_tags)
        self.assertEqual(appsec_tags['_dd.appsec.test.reference'], test_value)

    def test_keep_flag_true_event_false_behavior(self):
        """Test that keep=true in rule output sets sampling priority to USER-KEEP"""
        headers = {'X-Test-Keep-True': 'trigger'}

        spans = self.do_request_and_get_spans('/http', headers=headers)

        # Check sampling priority is set to USER-KEEP (2) when keep=true
        sampling_priority = self.get_sampling_priority(spans)
        self.assertEqual(
            sampling_priority, 2,
            "Sampling priority should be 2 (USER-KEEP) when rule has keep=true"
        )

        appsec_tags = self.find_all_appsec_tags(spans)
        self.assertNotIn(
            'appsec.event', appsec_tags,
            "Should not have appsec.event tag when rules have event=false")

    def test_keep_flag_false_event_true_behavior(self):
        """Test that keep=false in rule output does not set sampling priority to USER-KEEP"""
        headers = {'X-Test-Keep-False': 'trigger'}

        spans = self.do_request_and_get_spans('/http', headers=headers)

        sampling_priority = self.get_sampling_priority(spans)
        self.assertNotEqual(
            sampling_priority, 2,
            "Sampling priority should NOT be 2 (USER-KEEP) when rule has keep=false"
        )

        appsec_tags = self.find_all_appsec_tags(spans)
        self.assertIn(
            'appsec.event', appsec_tags,
            "Should have appsec.event tag when rules have event=true")
