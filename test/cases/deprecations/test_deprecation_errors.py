"""Test nginx config deprecation errors.

The directives `opentracing_load_tracer` and `datadog_load_tracer` cause nginx
to fail to load, and instead log an error diagnostic.
"""

from .. import case

from pathlib import Path


class TestDeprecationErrors(case.TestCase):

    def test_opentracing_load_tracer(self):
        return self.run_test_for_config('conf/opentracing_load_tracer.conf',
                                        'opentracing_load_tracer')

    def test_datadog_load_tracer(self):
        return self.run_test_for_config('conf/datadog_load_tracer.conf',
                                        'datadog_load_tracer')

    def run_test_for_config(self, config_relative_path, directive):
        config_path = Path(__file__).parent / config_relative_path
        config_text = config_path.read_text()
        status, log_lines = self.orch.nginx_test_config(
            config_text, config_path.name)

        self.assertNotEqual(status, 0)

        expected = f'The "{directive}" directive is no longer necessary.  Use "datadog {{ ... }}" to configure tracing.'
        self.assertTrue(any(expected in line for line in log_lines), {
            'expected': expected,
            'log_lines': log_lines
        })
