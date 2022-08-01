from .. import case
from .. import formats

from pathlib import Path


class TestOperationName(case.TestCase):
    def test_default_in_request(self):
        """Verify that the request span's operation name matches the default
        pattern when not otherwise configured.
        """

        # The default value is determined by
        # `TracingLibrary::default_request_operation_name_pattern`.
        def on_chunk(chunk):
            first, *rest = chunk
            self.assertEqual(0, len(rest), chunk)
            self.assertEqual('nginx.request', first['name'], chunk)

        return self.run_operation_name_test('./conf/default_in_request.conf',
                                            on_chunk)

    def test_default_in_location(self):
        """Verify that both the request span and the location span's operation
        name matches the default pattern when not otherwise configured.
        """

        # The default values are determined by
        # `TracingLibrary::default_request_operation_name_pattern` and
        # `TracingLibrary::default_location_operation_name_pattern`.
        def on_chunk(chunk):
            first, *rest = chunk
            self.assertEqual(1, len(rest), chunk)
            self.assertEqual('nginx.proxy_pass', first['name'], chunk)
            self.assertEqual('nginx.request', rest[0]['name'], chunk)

        return self.run_operation_name_test('./conf/default_in_location.conf',
                                            on_chunk)

    def test_manual_in_request_at_location(self):
        """Verify that using the `datadog_operation_name` directive in a
        `location` block causes the resulting request span's operation name to
        match the setting.
        """
        def on_chunk(chunk):
            first, *rest = chunk
            self.assertEqual(0, len(rest), chunk)
            self.assertEqual('go GET em /foo', first['name'], chunk)

        return self.run_operation_name_test(
            './conf/manual_in_request_at_location.conf', on_chunk)

    def test_manual_in_request_at_server(self):
        """Verify that using the `datadog_operation_name` directive in a
        `server` block causes the resulting request span's operation name to
        match the setting.
        """
        def on_chunk(chunk):
            first, *rest = chunk
            self.assertEqual(0, len(rest), chunk)
            self.assertEqual('go GET em /foo', first['name'], chunk)

        return self.run_operation_name_test(
            './conf/manual_in_request_at_server.conf', on_chunk)

    def test_manual_in_request_at_http(self):
        """Verify that using the `datadog_operation_name` directive in a `http`
        block causes the resulting request span's operation name to match the
        setting.
        """
        def on_chunk(chunk):
            first, *rest = chunk
            self.assertEqual(0, len(rest), chunk)
            self.assertEqual('go GET em /foo', first['name'], chunk)

        return self.run_operation_name_test(
            './conf/manual_in_request_at_http.conf', on_chunk)

    def test_manual_in_location_at_location(self):
        """Verify that using the `datadog_location_operation_name` directive in
        a `location` block causes the resulting location span's operation name
        to match the setting.  Note that the `datadog_trace_locations on`
        directive must also be used.
        """
        def on_chunk(chunk):
            first, *rest = chunk
            self.assertEqual(1, len(rest), chunk)
            # We assume that the location span comes first, because it finishes
            # first.
            self.assertEqual('go GET em /foo', first['name'], chunk)

        return self.run_operation_name_test(
            './conf/manual_in_location_at_location.conf', on_chunk)

    def test_manual_in_location_at_server(self):
        """Verify that using the `datadog_location_operation_name` directive in
        a `server` block causes the resulting location span's operation name
        to match the setting.  Note that the `datadog_trace_locations on`
        directive must also be used.
        """
        def on_chunk(chunk):
            first, *rest = chunk
            self.assertEqual(1, len(rest), chunk)
            # We assume that the location span comes first, because it finishes
            # first.
            self.assertEqual('go GET em /foo', first['name'], chunk)

        return self.run_operation_name_test(
            './conf/manual_in_location_at_server.conf', on_chunk)

    def test_manual_in_location_at_http(self):
        """Verify that using the `datadog_location_operation_name` directive in
        an `http` block causes the resulting location span's operation name to
        match the setting.  Note that the `datadog_trace_locations on`
        directive must also be used.
        """
        def on_chunk(chunk):
            first, *rest = chunk
            self.assertEqual(1, len(rest), chunk)
            # We assume that the location span comes first, because it finishes
            # first.
            self.assertEqual('go GET em /foo', first['name'], chunk)

        return self.run_operation_name_test(
            './conf/manual_in_location_at_http.conf', on_chunk)

    def run_operation_name_test(self, conf_relative_path, on_chunk):
        conf_path = Path(__file__).parent / conf_relative_path
        conf_text = conf_path.read_text()
        print('Before nginx_replace_config', flush=True)  # TODO: no
        status, log_lines = self.orch.nginx_replace_config(
            conf_text, conf_path.name)
        print('After nginx_replace_config', flush=True)  # TODO: no
        self.assertEqual(0, status, log_lines)

        # Clear any outstanding logs from the agent.
        print('Before sync_service("agent")')  # TODO
        self.orch.sync_service('agent')
        print('After sync_service("agent")')  # TODO

        print('Before send_nginx_http_request')  # TODO: no
        status, _ = self.orch.send_nginx_http_request('/foo')
        print('After send_nginx_http_request')  # TODO: no
        self.assertEqual(200, status)

        # Reload nginx to force it to send its traces.
        self.orch.reload_nginx()

        print('Before sync_service("agent")')  # TODO: no
        log_lines = self.orch.sync_service('agent')
        print('After sync_service("agent")')  # TODO: no
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
