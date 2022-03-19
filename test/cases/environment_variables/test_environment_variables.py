from .. import case

from pathlib import Path
import time  # TODO: no


class TestEnvironmentVariables(case.TestCase):
    def test_environment_variables(self):
        nginx_conf = (Path(__file__).parent / 'conf' /
                      'nginx.conf').read_text()
        extra_env = {'DD_TRACE_SAMPLING_RULES': '[]'}
        with self.orch.custom_nginx(nginx_conf, extra_env):
            # TODO: bad!
            time.sleep(5)
            status, body = self.orch.send_nginx_request('/', 8080)
            self.assertEqual(status, 200)
            self.assertEqual(body, b'I am a fish man []')
