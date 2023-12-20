from .. import case
from .. import formats

from pathlib import Path
from typing import Union


def nginx_config(conf_dir: Path, allow_delegation_in_subrequests: bool, delegate_to_upstream,
                 delegate_to_auth):
    in_subrequests = ''
    if allow_delegation_in_subrequests:
        in_subrequests = 'datadog_allow_sampling_delegation_in_subrequests on;'
    to_upstream = ''
    if delegate_to_upstream:
        to_upstream = 'datadog_delegate_sampling on;'
    to_auth = ''
    if delegate_to_auth:
        to_auth = 'datadog_delegate_sampling on;'

    template_path = conf_dir / 'nginx.template.conf'
    conf = template_path.read_text()
    conf = conf.replace('${allow_delegation_in_subrequests}', in_subrequests)
    conf = conf.replace('${delegate_to_upstream}', to_upstream)
    conf = conf.replace('${delegate_to_auth}', to_auth)

    return conf


class TestAuthRequest(case.TestCase):

    def run_test(
            self,
            allow_delegation_in_subrequests: bool,
            delegate_to_upstream: bool,
            delegate_to_auth: bool,
            expected_sampling_decider_service: Union[str, None],
            expected_nginx_sampling_priority: int,
            expected_nginx_sampling_mechanism: int,
            expected_upstream_sampling_priority: int,
            expected_upstream_sampling_mechanism: int,
            expected_auth_sampling_priority: int,
            expected_auth_sampling_mechanism: int):
        conf_dir = Path(__file__).parent / 'conf'
        conf = nginx_config(conf_dir, allow_delegation_in_subrequests,
                            delegate_to_upstream, delegate_to_auth)

        status, lines = self.orch.nginx_replace_config(conf, 'nginx.conf')
        self.assertEqual(0, status, lines)

        with self.orch.custom_nginx((conf_dir / 'upstream.conf').read_text(), healthcheck_port=8080), \
                self.orch.custom_nginx((conf_dir / 'auth.conf').read_text(), healthcheck_port=8081):
            # TODO
            # end TODO
            status, _, body = self.orch.send_nginx_http_request('/upstream')
            self.assertEqual(200, status, {'response_body': body})

        self.orch.reload_nginx()
        agent_log_lines = self.orch.sync_service('agent')
        # Trawl through the spans received by the agent, and fill out the
        # object below. Then we can compare it with the expected values.
        sampling_decider_service = None
        decisions = {} # {service: {"priority": int, "mechanism": int}}

        spans = formats.parse_spans(agent_log_lines)
        for span in spans:
            service = span['service']
            decider = span['meta'].get('_dd.is_sampling_decider')
            if decider == '1':
                self.assertIsNone(sampling_decider_service, {'span': span})
                sampling_decider_service = service
            decision = decisions.setdefault(service, {})
            priority = span.get('metrics', {}).get('_sampling_priority_v1')
            if priority is not None:
                self.assertIsNone(decision.get('priority'), {'span': span})
                decision['priority'] = int(priority)
            mechanism = span.get('meta', {}).get('_dd.p.dm')
            if mechanism is not None:
                self.assertIsNone(decision.get('mechanism'), {'span': span})
                # _dd.p.dm is the mechanism prefixed by a hyphen.
                mechanism = int(mechanism[1:])
                decision['mechanism'] = mechanism

        # Verify that the `decisions` we collected are consistent with the
        # `expected_*` values.
        context = {'spans': spans, 'decisions': decisions, 'response_body': body}

        self.assertEqual(sampling_decider_service, expected_sampling_decider_service, context)

        self.assertIsNotNone(decisions.get('nginx'), context)
        self.assertEqual(decisions['nginx'].get('priority'), expected_nginx_sampling_priority, context)
        self.assertEqual(decisions['nginx'].get('mechanism'), expected_nginx_sampling_mechanism, context)

        self.assertIsNotNone(decisions.get('upstream'), context)
        self.assertEqual(decisions['upstream'].get('priority'), expected_upstream_sampling_priority, context)
        self.assertEqual(decisions['upstream'].get('mechanism'), expected_upstream_sampling_mechanism, context)

        self.assertIsNotNone(decisions.get('auth'), context)
        self.assertEqual(decisions['auth'].get('priority'), expected_auth_sampling_priority, context)
        self.assertEqual(decisions['auth'].get('mechanism'), expected_auth_sampling_mechanism, context)

    # There are eight test cases -- one corresponding to each of the possible
    # combinations of the three boolean parameters:
    #
    # - allow_delegation_in_subrequests
    # - delegate_to_upstream
    # - delegate_to_auth

    def test_no_delegation(self):
        self.run_test(allow_delegation_in_subrequests=False,
                      delegate_to_upstream=False,
                      delegate_to_auth=False,
                      # "nginx" will make a default sampling decision, and
                      # the other services will honor it.
                      expected_sampling_decider_service=None,
                      expected_nginx_sampling_priority=1,
                      expected_nginx_sampling_mechanism=0,
                      expected_upstream_sampling_priority=1,
                      expected_upstream_sampling_mechanism=0,
                      expected_auth_sampling_priority=1,
                      expected_auth_sampling_mechanism=0)

    # TODO: the other seven
