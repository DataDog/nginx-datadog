import cases.case

from pathlib import Path


class TestDeprecationWarnings(cases.case.TestCase):
    def test_opentracing_trace_locations(self):
        directive = 'opentracing_trace_locations'
        return self.run_test_for_config(f'conf/{directive}.conf', directive)

    def run_test_for_config(self, config_relative_path, directive):
        config_path = Path(__file__).parent / config_relative_path
        config_text = config_path.read_text()
        status, log_lines = self.orch.nginx_test_config(
            config_text, config_path.name)

        self.assertEqual(status, 0)

        # old ="opentracing_something"
        # new = "datadog_something"
        old = directive
        new = 'datadog_' + old[len('opentracing_'):]
        expected = f'Backward compatibility with the "{old}" configuration directive is deprecated.  Please use "{new}" instead.'
        self.assertTrue(any(expected in line for line in log_lines), {
            'expected': expected,
            'log_lines': log_lines
        })
