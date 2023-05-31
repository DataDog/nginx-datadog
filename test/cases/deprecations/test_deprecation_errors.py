"""Test nginx config deprecation errors.

The directives `opentracing_load_tracer` and `datadog_load_tracer` cause nginx
to fail to load, and instead log an error diagnostic.
"""

from .. import case

from pathlib import Path


class TestDeprecationErrors(case.TestCase):

    def test_opentracing_load_tracer(self):
        return self.run_test_for_config(
            config_relative_path='conf/opentracing_load_tracer.conf',
            diagnostic_excerpt=
            'The "opentracing_load_tracer" directive is no longer necessary.')

    def test_datadog_load_tracer(self):
        return self.run_test_for_config(
            config_relative_path='conf/datadog_load_tracer.conf',
            diagnostic_excerpt=
            'The "datadog_load_tracer" directive is no longer necessary.')

    def test_datadog(self):
        return self.run_test_for_config(
            config_relative_path='conf/datadog.conf',
            diagnostic_excerpt=
            'The datadog { ... } block directive is no longer supported.')

    def run_test_for_config(self, config_relative_path, diagnostic_excerpt):
        config_path = Path(__file__).parent / config_relative_path
        config_text = config_path.read_text()
        status, log_lines = self.orch.nginx_test_config(
            config_text, config_path.name)

        self.assertNotEqual(status, 0)

        self.assertTrue(any(diagnostic_excerpt in line for line in log_lines),
                        {
                            'diagnostic_excerpt': diagnostic_excerpt,
                            'log_lines': log_lines
                        })
