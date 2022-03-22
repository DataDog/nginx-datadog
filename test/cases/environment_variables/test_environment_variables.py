from .. import case

from pathlib import Path
import time


def with_staggered_retries(thunk, retry_interval_seconds, max_attempts):
    while max_attempts:
        try:
            return thunk()
        except Exception:
            if max_attempts == 1:
                raise
            time.sleep(retry_interval_seconds)
            max_attempts -= 1


def parse_body(body):
    body = body.decode('utf8')
    result = {}
    for line in body.split('\n'):
        i_sep = line.index(' ')
        result[line[:i_sep]] = line[i_sep + 1:]
    return result


class TestEnvironmentVariables(case.TestCase):
    def test_environment_variables(self):
        nginx_conf = (Path(__file__).parent / 'conf' /
                      'nginx.conf').read_text()
        extra_env = {'DD_TRACE_SAMPLING_RULES': '[]'}
        extra_env = {
            'DD_AGENT_HOST': 'agent',
            'DD_ENV': 'prod',
            'DD_PROPAGATION_STYLE_EXTRACT': 'Datadog',
            'DD_PROPAGATION_STYLE_INJECT': 'Datadog',
            'DD_SERVICE': 'nginx',
            'DD_TAGS': 'foo:bar',
            'DD_TRACE_AGENT_PORT': '8126',
            'DD_TRACE_AGENT_URL': 'http://agent:8126',
            'DD_TRACE_ANALYTICS_ENABLED': 'true',
            'DD_TRACE_ANALYTICS_SAMPLE_RATE': '0',
            'DD_TRACE_CPP_LEGACY_OBFUSCATION': 'false',
            'DD_TRACE_DEBUG': 'false',
            'DD_TRACE_ENABLED': 'true',
            'DD_TRACE_RATE_LIMIT': '100',
            'DD_TRACE_REPORT_HOSTNAME': 'false',
            'DD_TRACE_SAMPLE_RATE': '1.0',
            'DD_TRACE_SAMPLING_RULES': '[]',
            'DD_TRACE_STARTUP_LOGS': '/dev/null',
            'DD_TRACE_TAGS_PROPAGATION_MAX_LENGTH': '512',
            'DD_VERSION': '1.2.3'
        }
        with self.orch.custom_nginx(nginx_conf, extra_env):
            # The just-started nginx probably isn't ready yet, so retry sending
            # the request a few times if it fails initially.
            status, body = with_staggered_retries(
                lambda: self.orch.send_nginx_http_request('/', 8080),
                retry_interval_seconds=0.25,
                max_attempts=10)
            self.assertEqual(status, 200)
            worker_env = parse_body(body)
            for variable_name, value in extra_env.items():
                self.assertTrue(variable_name in worker_env)
                self.assertEqual(value, worker_env[variable_name])
