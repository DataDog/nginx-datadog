from .. import case

from pathlib import Path


class TestDeprecationWarnings(case.TestCase):

    def test_opentracing_trace_locations(self):
        directive = "opentracing_trace_locations"
        return self.run_test_for_config(f"conf/{directive}.conf", directive)

    def test_opentracing_propagate_context(self):
        directive = "opentracing_propagate_context"
        return self.run_test_deprecated_1_2_0_directive(
            f"conf/{directive}.conf", directive)

    def test_opentracing_fastcgi_propagate_context(self):
        directive = "opentracing_fastcgi_propagate_context"
        return self.run_test_deprecated_1_2_0_directive(
            f"conf/{directive}.conf", directive)

    def test_opentracing_grpc_propagate_context(self):
        directive = "opentracing_grpc_propagate_context"
        return self.run_test_deprecated_1_2_0_directive(
            f"conf/{directive}.conf", directive)

    def test_opentracing_operation_name(self):
        directive = "opentracing_operation_name"
        return self.run_test_for_config(f"conf/{directive}.conf", directive)

    def test_opentracing_location_operation_name(self):
        directive = "opentracing_location_operation_name"
        return self.run_test_for_config(f"conf/{directive}.conf", directive)

    def test_opentracing_trust_incoming_span(self):
        directive = "opentracing_trust_incoming_span"
        return self.run_test_for_config(f"conf/{directive}.conf", directive)

    def test_opentracing_tag(self):
        directive = "opentracing_tag"
        return self.run_test_for_config(f"conf/{directive}.conf", directive)

    def test_datadog_enable_tag(self):
        directive = "datadog_enable"
        return self.run_test_deprecated_1_4_0_directive(
            f"conf/{directive}.conf", directive)

    def test_datadog_disable_tag(self):
        directive = "datadog_disable"
        return self.run_test_deprecated_1_4_0_directive(
            f"conf/{directive}.conf", directive)

    def run_test_for_config(self, config_relative_path, directive):
        config_path = Path(__file__).parent / config_relative_path
        config_text = config_path.read_text()
        status, log_lines = self.orch.nginx_test_config(
            config_text, config_path.name)

        self.assertEqual(status, 0)

        # old ="opentracing_something"
        # new = "datadog_something"
        old = directive
        new = directive.replace("opentracing_", "datadog_")
        expected = f'Backward compatibility with the "{old}" configuration directive is deprecated.  Please use "{new}" instead.'
        self.assertTrue(
            any(expected in line for line in log_lines),
            {
                "expected": expected,
                "log_lines": log_lines
            },
        )

    def run_test_deprecated_1_2_0_directive(self, config_relative_path,
                                            directive):
        config_path = Path(__file__).parent / config_relative_path
        config_text = config_path.read_text()
        status, log_lines = self.orch.nginx_test_config(
            config_text, config_path.name)

        self.assertEqual(status, 0)

        old = directive
        expected = f'Directive "{old}" is deprecated and can be removed since v1.2.0'
        self.assertTrue(
            any(expected in line for line in log_lines),
            {
                "expected": expected,
                "log_lines": log_lines
            },
        )

    def run_test_deprecated_1_4_0_directive(self, config_relative_path,
                                            directive):
        config_path = Path(__file__).parent / config_relative_path
        config_text = config_path.read_text()
        status, log_lines = self.orch.nginx_test_config(
            config_text, config_path.name)

        self.assertEqual(status, 0)

        old = directive
        expected = f'Directive "{old}" is deprecated and can be removed since v1.4.0'
        self.assertTrue(
            any(expected in line for line in log_lines),
            {
                "expected": expected,
                "log_lines": log_lines
            },
        )
