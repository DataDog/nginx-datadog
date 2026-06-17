"""Tests for JWT analysis via the decode-auth-jwt WAF processor (rules api-001-{100..130}).

The processor decodes the JWT from the Authorization header into server.request.jwt.
The rules_compat rules inspect the decoded JWT and set span attributes. No backend
endpoint changes are needed — the WAF reads directly from the authorization header.

JWTs used here are unsigned (empty signature); the WAF processor only decodes, it
does not verify signatures.

Pre-computed tokens (header.payload.):
  HS256 header  : {"alg":"HS256","typ":"JWT"}
  none header   : {"alg":"none","typ":"JWT"}
  payload no exp/aud  : {"sub":"test","iat":1516239022}
  payload w/ exp+aud  : {"sub":"test","iat":1516239022,"exp":9999999999,"aud":"api"}
"""

from pathlib import Path

from .. import case, formats

_JWT_HS256_NO_EXP_NO_AUD = ("eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9"
                            ".eyJzdWIiOiJ0ZXN0IiwiaWF0IjoxNTE2MjM5MDIyfQ"
                            ".")
_JWT_HS256_WITH_EXP_WITH_AUD = (
    "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9"
    ".eyJzdWIiOiJ0ZXN0IiwiaWF0IjoxNTE2MjM5MDIyLCJleHAiOjk5OTk5OTk5OTksImF1ZCI6ImFwaSJ9"
    ".")
_JWT_NONE_ALG = ("eyJhbGciOiJub25lIiwidHlwIjoiSldUIn0"
                 ".eyJzdWIiOiJ0ZXN0IiwiaWF0IjoxNTE2MjM5MDIyfQ"
                 ".")


class TestJwt(case.TestCase):
    requires_waf = True
    _config_done = False

    @classmethod
    def setUpClass(cls):
        cls._config_done = False
        super().setUpClass()

    def setUp(self):
        super().setUp()
        if not TestJwt._config_done:
            waf_path = Path(__file__).parent / "conf/waf.json"
            self.orch.nginx_replace_file("/tmp/waf.json", waf_path.read_text())

            conf_path = Path(__file__).parent / "conf/http.conf"
            status, log_lines = self.orch.nginx_replace_config(
                conf_path.read_text(), conf_path.name)
            self.assertEqual(0, status, log_lines)
            TestJwt._config_done = True

        self.orch.sync_service("agent")

    def _spans_for_request(self, headers):
        status, _, _ = self.orch.send_nginx_http_request("/http",
                                                         80,
                                                         headers=headers)
        self.assertEqual(status, 200)
        self.orch.reload_nginx()
        log_lines = self.orch.sync_service("agent")
        spans = []
        for line in log_lines:
            entry = formats.parse_trace(line)
            if entry is not None:
                for trace in entry:
                    spans.extend(trace)
        return spans

    def _get_meta(self, spans):
        meta = {}
        for span in spans:
            meta.update(span.get("meta", {}))
        return meta

    def _get_metrics(self, spans):
        metrics = {}
        for span in spans:
            metrics.update(span.get("metrics", {}))
        return metrics

    def test_no_expiry_tag_set_when_exp_missing(self):
        """api-001-100: _dd.appsec.api.jwt.no_expiry is set when JWT has no exp claim."""
        spans = self._spans_for_request(
            {"Authorization": f"Bearer {_JWT_HS256_NO_EXP_NO_AUD}"})
        metrics = self._get_metrics(spans)
        self.assertIn(
            "_dd.appsec.api.jwt.no_expiry",
            metrics,
            f"Expected _dd.appsec.api.jwt.no_expiry in metrics; got metrics={metrics!r}",
        )

    def test_no_expiry_tag_absent_when_exp_present(self):
        """api-001-100: _dd.appsec.api.jwt.no_expiry is absent when JWT has an exp claim."""
        spans = self._spans_for_request(
            {"Authorization": f"Bearer {_JWT_HS256_WITH_EXP_WITH_AUD}"})
        metrics = self._get_metrics(spans)
        self.assertNotIn(
            "_dd.appsec.api.jwt.no_expiry",
            metrics,
            f"Expected _dd.appsec.api.jwt.no_expiry to be absent; got metrics={metrics!r}",
        )

    def test_algorithm_collected(self):
        """api-001-110: api.security.jwt.alg is set to the algorithm from the JWT header."""
        spans = self._spans_for_request(
            {"Authorization": f"Bearer {_JWT_HS256_NO_EXP_NO_AUD}"})
        meta = self._get_meta(spans)
        self.assertEqual(
            meta.get("api.security.jwt.alg"),
            "HS256",
            f"Expected api.security.jwt.alg='HS256'; got meta={meta!r}",
        )

    def test_no_audience_tag_set_when_aud_missing(self):
        """api-001-120: _dd.appsec.api.jwt.no_audience is set when JWT has no aud claim."""
        spans = self._spans_for_request(
            {"Authorization": f"Bearer {_JWT_HS256_NO_EXP_NO_AUD}"})
        metrics = self._get_metrics(spans)
        self.assertIn(
            "_dd.appsec.api.jwt.no_audience",
            metrics,
            f"Expected _dd.appsec.api.jwt.no_audience in metrics; got metrics={metrics!r}",
        )

    def test_no_audience_tag_absent_when_aud_present(self):
        """api-001-120: _dd.appsec.api.jwt.no_audience is absent when JWT has an aud claim."""
        spans = self._spans_for_request(
            {"Authorization": f"Bearer {_JWT_HS256_WITH_EXP_WITH_AUD}"})
        metrics = self._get_metrics(spans)
        self.assertNotIn(
            "_dd.appsec.api.jwt.no_audience",
            metrics,
            f"Expected _dd.appsec.api.jwt.no_audience to be absent; got metrics={metrics!r}",
        )

    def test_none_alg_tag_set(self):
        """api-001-130: _dd.appsec.api.jwt.none_alg is set when JWT uses alg=none."""
        spans = self._spans_for_request(
            {"Authorization": f"Bearer {_JWT_NONE_ALG}"})
        metrics = self._get_metrics(spans)
        self.assertIn(
            "_dd.appsec.api.jwt.none_alg",
            metrics,
            f"Expected _dd.appsec.api.jwt.none_alg in metrics; got metrics={metrics!r}",
        )

    def test_none_alg_tag_absent_for_hs256(self):
        """api-001-130: _dd.appsec.api.jwt.none_alg is absent for a normal HS256 JWT."""
        spans = self._spans_for_request(
            {"Authorization": f"Bearer {_JWT_HS256_NO_EXP_NO_AUD}"})
        metrics = self._get_metrics(spans)
        self.assertNotIn(
            "_dd.appsec.api.jwt.none_alg",
            metrics,
            f"Expected _dd.appsec.api.jwt.none_alg to be absent; got metrics={metrics!r}",
        )
