from .. import case
from .. import formats

from pathlib import Path


class Absent:
    """Denote the expected absence of a dict key, for use with `value_match`.
    """

    def __repr__(self):
        return 'Absent()'


def value_match(subject, pattern):
    if isinstance(pattern, dict):
        return all((k in subject and value_match(subject[k], v)) or (
            k not in subject and isinstance(v, Absent))
                   for k, v in pattern.items())
    return subject == pattern


class TestSamplingDelegation(case.TestCase):

    def run_test(self, endpoint, check_output):
        self.orch.sync_service('agent')

        conf_dir = Path(__file__).parent / 'conf'
        conf_path = conf_dir / 'nginx1.conf'
        status, lines = self.orch.nginx_replace_config(conf_path.read_text(),
                                                       conf_path.name)
        self.assertEqual(0, status, lines)

        with self.orch.custom_nginx((conf_dir / 'nginx2.conf').read_text(), healthcheck_port=8080) as nginx2, \
                self.orch.custom_nginx((conf_dir / 'nginx3.conf').read_text(), healthcheck_port=8081) as nginx3:
            status, _, body = self.orch.send_nginx_http_request(endpoint)
            self.assertEqual(200, status, body)

        self.orch.reload_nginx()
        log_lines = self.orch.sync_service('agent')
        check_output(response_body=body, agent_log_lines=log_lines)

    def test_zero_layers(self):
        """Verify that the sampling decision is made at the head when delegation
        is disabled, which is the default.
        """

        def check_output(response_body, agent_log_lines):
            spans = formats.parse_spans(agent_log_lines)

            def pattern(service):
                return {
                    'service': service,
                    'metrics': {
                        '_sampling_priority_v1': 1
                    },
                    'meta': {
                        '_dd.is_sampling_decider': Absent(),
                        '_dd.p.dm': '-0'
                    }
                }

            # The head service (nginx1) will make a default sampling decision of
            # "keep."
            self.assert_has_matching_span(spans, pattern('nginx1'))
            # The second service (nginx2) will honor the head's sampling decision.
            self.assert_has_matching_span(spans, pattern('nginx2'))
            # And the third.
            self.assert_has_matching_span(spans, pattern('nginx3'))

        self.run_test('/no-delegate/no-delegate/keep', check_output)

    def test_one_layer(self):
        """Verify that nginx can delegate sampling to an upstream.
        """

        def check_output(response_body, agent_log_lines):
            spans = formats.parse_spans(agent_log_lines)

            def pattern(service, is_decider=Absent()):
                return {
                    'service': service,
                    'metrics': {
                        '_sampling_priority_v1': 1
                    },
                    'meta': {
                        '_dd.is_sampling_decider': is_decider,
                        '_dd.p.dm': '-0'
                    }
                }

            self.assert_has_matching_span(spans,
                                          pattern('nginx1', is_decider='0'))
            self.assert_has_matching_span(spans,
                                          pattern('nginx2', is_decider='1'))
            self.assert_has_matching_span(spans, pattern('nginx3'))

        self.run_test('/delegate/no-delegate/keep', check_output)

    def test_two_layers(self):
        """Verify that sampling delegation can be chained.
        """

        def check_output(response_body, agent_log_lines):
            spans = formats.parse_spans(agent_log_lines)

            def pattern(service, is_decider=Absent()):
                return {
                    'service': service,
                    'metrics': {
                        # Now the sampling priority is 2, because nginx3's /keep
                        # endpoint uses an explicit sample rate of 1.0, as
                        # opposed to the default sampling that would result in a
                        # priority of 0 or 1.
                        '_sampling_priority_v1': 2
                    },
                    'meta': {
                        '_dd.is_sampling_decider': is_decider,
                        # The "decision maker" has sampling mechanism "3" for "rule".
                        '_dd.p.dm': '-3'
                    }
                }

            self.assert_has_matching_span(spans,
                                          pattern('nginx1', is_decider='0'))
            self.assert_has_matching_span(spans, pattern('nginx2'))
            self.assert_has_matching_span(spans,
                                          pattern('nginx3', is_decider='1'))

        self.run_test('/delegate/delegate/keep', check_output)

    def assert_has_matching_span(self, spans, pattern):
        self.assertTrue(any(value_match(span, pattern) for span in spans), {
            'pattern': pattern,
            'spans': spans
        })
