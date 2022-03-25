from .. import case
from .. import formats

from pathlib import Path


class TestOperationName(case.TestCase):
    def test_default_in_request(self):
        """Verify that the request span's operation name matches the default
        pattern when not otherwise configured.
        """
        def on_chunk(chunk):
            root, *rest = chunk
            self.assertEqual(0, len(rest), chunk)
            self.assertEqual('GET /foo', root['name'], chunk)

        return self.run_operation_name_test('./conf/default_in_request.conf',
                                            on_chunk)

    def test_default_in_location(self):
        """Verify that both the request span and the location span's operation
        name matches the default pattern when not otherwise configured.
        """
        def on_chunk(chunk):
            root, *rest = chunk
            self.assertEqual(1, len(rest), chunk)
            self.assertEqual('GET /foo', root['name'], chunk)
            self.assertEqual('GET /foo', rest[0]['name'], chunk)

        return self.run_operation_name_test('./conf/default_in_location.conf',
                                            on_chunk)

    def run_operation_name_test(self, conf_relative_path, on_chunk):
        # The default pattern is in `tracing_library.cpp':
        #
        #     string_view TracingLibrary::default_operation_name_pattern() {
        #         return "$request_method $uri";
        #     }
        conf_path = Path(__file__).parent / conf_relative_path
        conf_text = conf_path.read_text()
        status, log_lines = self.orch.nginx_replace_config(
            conf_text, conf_path.name)
        self.assertEqual(0, status, log_lines)

        # Clear any outstanding logs from the agent.
        self.orch.sync_service('agent')

        # GET /foo, expected operation name is "GET /foo"
        status, _ = self.orch.send_nginx_http_request('/foo')
        self.assertEqual(200, status)

        # Reload nginx to force it to send its traces.
        self.orch.reload_nginx()

        log_lines = self.orch.sync_service('agent')
        # Find the trace that came from nginx, and pass its chunks (groups of
        # spans) to the callback.
        found_nginx_trace = False
        for line in log_lines:
            trace = formats.parse_trace(line)
            if trace is None:
                # not a trace; some other logging
                continue
            for chunk in trace:
                if chunk[0]['service'] != 'nginx':
                    continue
                found_nginx_trace = True
                on_chunk(chunk)

        self.assertTrue(found_nginx_trace)
