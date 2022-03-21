from .. import case

from pathlib import Path
import time  # TODO: no


def with_staggered_retries(thunk, retry_interval_seconds, max_attempts):
    while max_attempts:
        try:
            return thunk()
        except Exception:
            if max_attempts == 1:
                raise
            time.sleep(retry_interval_seconds)
            max_attempts -= 1


class TestEnvironmentVariables(case.TestCase):
    def test_environment_variables(self):
        nginx_conf = (Path(__file__).parent / 'conf' /
                      'nginx.conf').read_text()
        extra_env = {'DD_TRACE_SAMPLING_RULES': '[]'}
        with self.orch.custom_nginx(nginx_conf, extra_env):
            # The just-started nginx probably isn't ready yet, so retry sending
            # the request a few times if it fails initially.
            status, body = with_staggered_retries(
                lambda: self.orch.send_nginx_request('/', 8080),
                retry_interval_seconds=0.25,
                max_attempts=10)
            self.assertEqual(status, 200)
            self.assertEqual(body, b'I am a fish man []')
