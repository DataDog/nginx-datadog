from .. import case
from .. import formats

from pathlib import Path


class TestEndpointRenaming(case.TestCase):
    """Test cases for endpoint renaming (http.endpoint tag generation).

    The endpoint renaming feature generates http.endpoint tags by analyzing
    URL paths and replacing dynamic segments with patterns like {param:int},
    {param:hex}, etc.
    """

    last_config = ''

    def replace_config(self, new_config):
        """Replace nginx configuration if different from last config."""
        if new_config != TestEndpointRenaming.last_config:
            conf_path = Path(__file__).parent / "conf" / new_config
            conf_text = conf_path.read_text()
            status, log_lines = self.orch.nginx_replace_config(
                conf_text, conf_path.name)
            self.assertEqual(0, status, log_lines)
            TestEndpointRenaming.last_config = new_config

    def send_request_and_get_span(self, url, config_name):
        self.replace_config(config_name)
        self.orch.sync_service('agent')

        status, _, _ = self.orch.send_nginx_http_request(url)

        self.orch.reload_nginx()
        log_lines = self.orch.sync_service('agent')

        spans = formats.parse_spans(log_lines)
        nginx_spans = [s for s in spans if s.get('service') == 'test-service']

        self.assertEqual(len(nginx_spans), 1, "Expected exactly one span")

        return status, nginx_spans[0]

    def test_endpoint_renaming_disabled_by_default(self):
        """Verify that endpoint renaming is disabled by default.

        When disabled, no http.endpoint tag should be added to spans.
        """
        status, span = self.send_request_and_get_span("/api/users/123",
                                                      "disabled.conf")
        self.assertEqual(200, status)

        meta = span.get('meta', {})
        self.assertNotIn(
            'http.endpoint', meta,
            f"http.endpoint should not be present when disabled: {meta}")

    def test_endpoint_renaming_fallback_mode(self):
        """Verify endpoint renaming in fallback mode.

        When enabled in fallback mode, http.endpoint should be added
        when http.route is not present. For this endpoint, it's not present
        """
        status, span = self.send_request_and_get_span("/api/users/123",
                                                      "fallback.conf")
        self.assertEqual(200, status)

        meta = span.get('meta', {})
        self.assertIn('http.endpoint', meta,
                      "http.endpoint should be present in fallback mode")
        self.assertEqual(
            meta['http.endpoint'], '/api/users/{param:int}',
            f"Expected /api/users/{{param:int}}, got {meta['http.endpoint']}")

    def test_fallback_mode_respects_http_route(self):
        """Verify fallback mode does NOT calculate endpoint when http.route is set."""
        status, span = self.send_request_and_get_span("/api/orders/789",
                                                      "fallback.conf")
        self.assertEqual(200, status)

        meta = span.get('meta', {})
        # In fallback mode with http.route set, http.endpoint should NOT be present
        self.assertIn('http.route', meta, "http.route should be present")
        self.assertEqual(meta['http.route'], '/api/orders/:id')
        self.assertNotIn(
            'http.endpoint', meta,
            "http.endpoint should NOT be set when http.route exists in fallback mode"
        )

    def test_endpoint_renaming_always_mode(self):
        """Verify endpoint renaming in always mode.

        When enabled in always mode, http.endpoint should always be calculated,
        even when http.route is present.
        """
        status, span = self.send_request_and_get_span(
            "/api/products/abc-123-def", "always.conf")
        self.assertEqual(200, status)

        meta = span.get('meta', {})
        self.assertIn('http.endpoint', meta,
                      "http.endpoint should be present in always mode")
        # Verify the pattern: /api/products/abc-123-def -> /api/products/{param:hex_id}
        self.assertEqual(
            meta['http.endpoint'], '/api/products/{param:hex_id}',
            f"Expected /api/products/{{param:hex_id}}, got {meta['http.endpoint']}"
        )

    def test_always_mode_ignores_http_route(self):
        """Verify always mode calculates endpoint even when http.route is set."""
        status, span = self.send_request_and_get_span("/api/orders/999",
                                                      "always.conf")
        self.assertEqual(200, status)

        meta = span.get('meta', {})
        # In always mode, both http.route and http.endpoint should be present
        self.assertIn('http.route', meta, "http.route should be present")
        self.assertIn('http.endpoint', meta,
                      "http.endpoint should be present in always mode")
        self.assertEqual(meta['http.route'], '/api/orders/:id')
        self.assertEqual(meta['http.endpoint'], '/api/orders/{param:int}')

    def test_appsec_enables_fallback_mode_by_default(self):
        """Verify that when appsec is enabled, endpoint renaming defaults to fallback mode.

        With appsec enabled and no explicit resource_renaming_enabled directive,
        the feature should be enabled in fallback mode:
        - http.endpoint calculated when http.route not present
        - http.endpoint NOT calculated when http.route is present
        """
        if self.waf_disabled:
            self.skipTest("WAF is disabled - appsec test requires WAF support")

        # Test without http.route - should calculate endpoint
        status, span = self.send_request_and_get_span("/api/users/456",
                                                      "appsec_enabled.conf")
        self.assertEqual(200, status)

        meta = span.get('meta', {})
        self.assertIn(
            'http.endpoint', meta,
            "http.endpoint should be present when appsec is enabled (fallback mode)"
        )
        self.assertEqual(
            meta['http.endpoint'], '/api/users/{param:int}',
            f"Expected /api/users/{{param:int}}, got {meta['http.endpoint']}")

        # Test with http.route - should NOT calculate endpoint (fallback mode)
        status, span = self.send_request_and_get_span("/api/orders/555",
                                                      "appsec_enabled.conf")
        self.assertEqual(200, status)

        meta = span.get('meta', {})
        self.assertIn('http.route', meta, "http.route should be present")
        self.assertEqual(meta['http.route'], '/api/orders/:id')
        self.assertNotIn(
            'http.endpoint', meta,
            "http.endpoint should NOT be set when http.route exists in fallback mode (appsec enabled)"
        )
