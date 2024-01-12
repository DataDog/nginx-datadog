from .. import case

import json
from pathlib import Path


def find_mismatches(pattern, subject):
    """Return a list of all of the ways that `subject` differs from `pattern`.

    `pattern` is a composition of `dict` and `list`, where each `dict` asserts
    keys that the corresponding `dict` within `subject` must have, and the value
    at each key must match the corresponding pattern in `pattern`.

    Corresponding lists in `pattern` and `subject` must have the same length,
    and the elements in `subject` must match the corresponding patterns in
    `pattern`.

    For example, the following invocation

        find_mismatches(
            pattern={'foo': [1, 2], 'bar': {'baz': 3}},
            subject={'foo': [1, 2], 'boo': 4, 'bar': {'bax': 3}})

    would return a list containing the one mismatch between `pattern` and
    `subject`:

        [{'path': '.bar', 'error': 'missing key', 'key': 'baz', 'actual': {'bax': 3}}]

    because the value at the subject's "bar" key does not have a "baz" key.

    If there are no mismatches, then return an empty `list`.
    """

    def yield_mismatches(path, pattern, subject):
        if type(pattern) is not type(subject):
            yield {
                'path': path,
                'error': 'mismatched types',
                'pattern': pattern,
                'actual': subject
            }
        elif isinstance(pattern, list):
            if len(pattern) != len(subject):
                yield {
                    'path': path,
                    'error': 'mismatched list lengths',
                    'pattern': pattern,
                    'actual': subject
                }
                return
            for i in range(len(pattern)):
                yield from yield_mismatches(path + f'.{i}', pattern[i],
                                            subject[i])
        elif isinstance(pattern, dict):
            for key, subpattern in pattern.items():
                if key not in subject:
                    yield {
                        'path': path,
                        'error': 'missing key',
                        'key': key,
                        'actual': subject
                    }
                else:
                    yield from yield_mismatches(f'{path}.{key}', subpattern,
                                                subject[key])
        elif pattern != subject:
            yield {
                'path': path,
                'error': 'mismatched values',
                'expected': pattern,
                'actual': subject
            }

    return list(yield_mismatches('', pattern, subject))


class TestConfiguration(case.TestCase):

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.default_config = None

    def test_in_http(self):
        conf_path = Path(__file__).parent / 'conf' / 'in_http.conf'
        conf_text = conf_path.read_text()

        status, log_lines = self.orch.nginx_replace_config(
            conf_text, conf_path.name)
        self.assertEqual(0, status, log_lines)

        status, _, body = self.orch.send_nginx_http_request('/')
        self.assertEqual(200, status)

        config = json.loads(body)
        # See conf/in_http.conf, which contains the following:
        #
        #     datadog_service_name foosvc;
        #     datadog_environment fooment;
        #     datadog_agent_url http://bogus:1234;
        #     datadog_propagation_styles B3 Datadog;
        pattern = {
            'defaults': {
                'service': 'foosvc',
                'environment': 'fooment'
            },
            'collector': {
                'config': {
                    'traces_url': 'http://bogus:1234/v0.4/traces'
                }
            },
            'injection_styles': ['B3', 'Datadog'],
            'extraction_styles': ['B3', 'Datadog']
        }

        mismatches = find_mismatches(pattern, config)
        self.assertEqual(mismatches, [])

    def run_error_test(self, conf_relative_path, diagnostic_excerpt):
        conf_path = Path(__file__).parent / conf_relative_path
        conf_text = conf_path.read_text()

        status, log_lines = self.orch.nginx_test_config(
            conf_text, conf_path.name)
        self.assertNotEqual(0, status)
        self.assertTrue(any(diagnostic_excerpt in line for line in log_lines),
                        log_lines)

    def test_duplicate_service_name(self):
        self.run_error_test(
            conf_relative_path='./conf/duplicate/service_name.conf',
            diagnostic_excerpt='Duplicate call to "datadog_service_name"')

    def test_duplicate_environment(self):
        self.run_error_test(
            conf_relative_path='./conf/duplicate/environment.conf',
            diagnostic_excerpt='Duplicate call to "datadog_environment"')

    def test_duplicate_agent_url(self):
        self.run_error_test(
            conf_relative_path='./conf/duplicate/agent_url.conf',
            diagnostic_excerpt='Duplicate call to "datadog_agent_url"')

    def test_duplicate_propagation_styles(self):
        self.run_error_test(
            conf_relative_path='./conf/duplicate/propagation_styles.conf',
            diagnostic_excerpt=
            'Datadog propagation styles are already configured.')

    def test_propagation_styles_error(self):
        return self.run_error_test(
            conf_relative_path='./conf/propagation_styles_error.conf',
            diagnostic_excerpt=
            'Datadog propagation styles are already configured.')

    def run_wrong_block_test(self, conf_relative_path):
        conf_path = Path(__file__).parent / conf_relative_path
        conf_text = conf_path.read_text()

        status, log_lines = self.orch.nginx_test_config(
            conf_text, conf_path.name)
        self.assertNotEqual(0, status)
        excerpt = 'directive is not allowed here'
        self.assertTrue(any(excerpt in line for line in log_lines), log_lines)

    def test_error_in_main_service_name(self):
        return self.run_wrong_block_test(
            './conf/error_in_main/service_name.conf')

    def test_error_in_main_environment(self):
        return self.run_wrong_block_test(
            './conf/error_in_main/environment.conf')

    def test_error_in_main_agent_url(self):
        return self.run_wrong_block_test('./conf/error_in_main/agent_url.conf')

    def test_error_in_main_propagation_styles(self):
        return self.run_wrong_block_test(
            './conf/error_in_main/propagation_styles.conf')

    def test_error_in_server_service_name(self):
        return self.run_wrong_block_test(
            './conf/error_in_server/service_name.conf')

    def test_error_in_server_environment(self):
        return self.run_wrong_block_test(
            './conf/error_in_server/environment.conf')

    def test_error_in_server_agent_url(self):
        return self.run_wrong_block_test(
            './conf/error_in_server/agent_url.conf')

    def test_error_in_server_propagation_styles(self):
        return self.run_wrong_block_test(
            './conf/error_in_server/propagation_styles.conf')
