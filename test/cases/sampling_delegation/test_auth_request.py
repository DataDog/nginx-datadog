from .. import case
from .. import formats

from pathlib import Path
from typing import Union


def nginx_config(conf_dir: Path, trace_subrequests: bool,
                 allow_delegation_in_subrequests: bool,
                 delegate_to_upstream: bool, delegate_to_auth: bool):
    tracing = ''
    if trace_subrequests:
        tracing = 'log_subrequest on;'
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
    conf = conf.replace('${trace_subrequests}', tracing)
    conf = conf.replace('${allow_delegation_in_subrequests}', in_subrequests)
    conf = conf.replace('${delegate_to_upstream}', to_upstream)
    conf = conf.replace('${delegate_to_auth}', to_auth)

    return conf


class TestAuthRequest(case.TestCase):

    def run_test(self, trace_subrequests: bool,
                 allow_delegation_in_subrequests: bool,
                 delegate_to_upstream: bool, delegate_to_auth: bool,
                 expected_sampling_decider_service: Union[str, None],
                 expected_nginx_sampling_priority: int,
                 expected_nginx_sampling_mechanism: int,
                 expected_upstream_sampling_priority: int,
                 expected_upstream_sampling_mechanism: int,
                 expected_auth_sampling_priority: int,
                 expected_auth_sampling_mechanism: int):
        conf_dir = Path(__file__).parent / 'conf'
        conf = nginx_config(conf_dir, trace_subrequests,
                            allow_delegation_in_subrequests,
                            delegate_to_upstream, delegate_to_auth)

        status, lines = self.orch.nginx_replace_config(conf, 'nginx.conf')
        self.assertEqual(0, status, lines)

        with self.orch.custom_nginx((conf_dir / 'upstream.conf').read_text(), healthcheck_port=8080), \
                self.orch.custom_nginx((conf_dir / 'auth.conf').read_text(), healthcheck_port=8081):
            status, _, body = self.orch.send_nginx_http_request(
                '/upstream', headers={'X-Datadog-Origin': 'Belgium'})
            self.assertEqual(200, status, {'response_body': body})

        self.orch.reload_nginx()
        agent_log_lines = self.orch.sync_service('agent')
        # Trawl through the spans received by the agent, and fill out the
        # object below. Then we can compare it with the expected values.
        sampling_decider_service = None
        decisions = {}  # {service: {"priority": int, "mechanism": int}}

        spans = formats.parse_spans(agent_log_lines)
        for span in spans:
            context = {'span': span, 'spans': spans}
            service = span['service']
            decider = span['meta'].get('_dd.is_sampling_decider')
            if decider == '1':
                self.assertIsNone(sampling_decider_service, context)
                sampling_decider_service = service
            decision = decisions.setdefault(service, {})
            priority = span.get('metrics', {}).get('_sampling_priority_v1')
            if priority is not None:
                self.assertIsNone(decision.get('priority'), context)
                decision['priority'] = int(priority)
            mechanism = span.get('meta', {}).get('_dd.p.dm')
            if mechanism is not None:
                self.assertIsNone(decision.get('mechanism'), context)
                # _dd.p.dm is the mechanism prefixed by a hyphen.
                mechanism = int(mechanism[1:])
                decision['mechanism'] = mechanism

        # Verify that the `decisions` we collected are consistent with the
        # `expected_*` values.
        context = {
            'spans': spans,
            'decisions': decisions,
            'response_body': body
        }

        self.assertEqual(sampling_decider_service,
                         expected_sampling_decider_service, context)

        self.assertIsNotNone(decisions.get('nginx'), context)
        self.assertEqual(decisions['nginx'].get('priority'),
                         expected_nginx_sampling_priority, context)
        self.assertEqual(decisions['nginx'].get('mechanism'),
                         expected_nginx_sampling_mechanism, context)

        self.assertIsNotNone(decisions.get('upstream'), context)
        self.assertEqual(decisions['upstream'].get('priority'),
                         expected_upstream_sampling_priority, context)
        self.assertEqual(decisions['upstream'].get('mechanism'),
                         expected_upstream_sampling_mechanism, context)

        self.assertIsNotNone(decisions.get('auth'), context)
        self.assertEqual(decisions['auth'].get('priority'),
                         expected_auth_sampling_priority, context)
        self.assertEqual(decisions['auth'].get('mechanism'),
                         expected_auth_sampling_mechanism, context)

    # There are sixteen configurations to try -- one corresponding to each of
    # the possible combinations of the four boolean parameters:
    #
    # - trace_subrequests
    # - allow_delegation_in_subrequests
    # - delegate_to_upstream
    # - delegate_to_auth
    #
    # These tests cover a subset of those cases, probing the behavior of likely
    # configurations and mis-configurations.

    def test_no_delegation(self):
        # With subrequest tracing disabled.
        self.run_test(
            trace_subrequests=False,  # <----
            allow_delegation_in_subrequests=False,
            delegate_to_upstream=False,
            delegate_to_auth=False,
            # "nginx" will make a default sampling decision.
            # "auth" will create its own trace, since tracing subrequests is
            # disabled.
            # "upstream" will honor "nginx"'s decision.
            expected_sampling_decider_service=None,
            expected_nginx_sampling_priority=1,
            expected_nginx_sampling_mechanism=0,
            expected_upstream_sampling_priority=1,
            expected_upstream_sampling_mechanism=0,
            expected_auth_sampling_priority=2,
            expected_auth_sampling_mechanism=3)

        # With subrequest tracing enabled.
        self.run_test(
            trace_subrequests=True,  # <----
            allow_delegation_in_subrequests=False,
            delegate_to_upstream=False,
            delegate_to_auth=False,
            # "nginx" will make a default sampling decision.
            # "auth" and "upstream" will honor "nginx"'s decision.
            # "upstream" will honor "nginx"'s decision.
            expected_sampling_decider_service=None,
            expected_nginx_sampling_priority=1,
            expected_nginx_sampling_mechanism=0,
            expected_upstream_sampling_priority=1,
            expected_upstream_sampling_mechanism=0,
            expected_auth_sampling_priority=1,
            expected_auth_sampling_mechanism=0)

    def test_delegate_to_upstream_only(self):
        # With subrequest tracing disabled.
        self.run_test(
            trace_subrequests=False,  # <----
            allow_delegation_in_subrequests=False,
            delegate_to_upstream=True,  # <----
            delegate_to_auth=False,
            # "nginx" will delegate to "upstream", which will make a decision.
            # "auth" will create its own trace, since tracing subrequests is
            # disabled.
            expected_sampling_decider_service="upstream",
            expected_nginx_sampling_priority=2,
            expected_nginx_sampling_mechanism=3,
            expected_upstream_sampling_priority=2,
            expected_upstream_sampling_mechanism=3,
            expected_auth_sampling_priority=2,
            expected_auth_sampling_mechanism=3)

        # With subrequest tracing enabled.
        self.run_test(
            trace_subrequests=True,  # <----
            allow_delegation_in_subrequests=False,
            delegate_to_upstream=True,  # <----
            delegate_to_auth=False,
            # "nginx" will delegate to "upstream", which will make a decision.
            # "auth" will honor nginx's provisional decision instead.
            expected_sampling_decider_service='upstream',
            expected_nginx_sampling_priority=2,
            expected_nginx_sampling_mechanism=3,
            expected_upstream_sampling_priority=2,
            expected_upstream_sampling_mechanism=3,
            expected_auth_sampling_priority=1,
            expected_auth_sampling_mechanism=0)

    def test_delegate_to_auth_only(self):
        # There are three things that need to happen for delegation to an auth
        # subrequest to actually happen:
        #
        # - `log_subrequest on;`
        # - `datadog_allow_delegation_in_subrequests on;`
        # - `datadog_delegate_sampling;` in the auth service's `location`.

        # If logging subrequests is disabled, then neither of the other two
        # options make any difference.
        self.run_test(
            trace_subrequests=False,  # <----
            allow_delegation_in_subrequests=True,  # <----
            delegate_to_upstream=False,
            delegate_to_auth=True,  # <----
            # "nginx" will make a default sampling decision.
            # "upstream" will honor "nginx"'s decision.
            # "auth" will create its own trace, since tracing of subrequests is
            # disabled.
            expected_sampling_decider_service=None,
            expected_nginx_sampling_priority=1,
            expected_nginx_sampling_mechanism=0,
            expected_upstream_sampling_priority=1,
            expected_upstream_sampling_mechanism=0,
            expected_auth_sampling_priority=2,
            expected_auth_sampling_mechanism=3)

        self.run_test(
            trace_subrequests=False,  # <----
            allow_delegation_in_subrequests=False,  # <----
            delegate_to_upstream=False,
            delegate_to_auth=True,  # <----
            # "nginx" will make a default sampling decision.
            # "upstream" will honor "nginx"'s decision.
            # "auth" will create its own trace, since tracing of subrequests is
            # disabled.
            expected_sampling_decider_service=None,
            expected_nginx_sampling_priority=1,
            expected_nginx_sampling_mechanism=0,
            expected_upstream_sampling_priority=1,
            expected_upstream_sampling_mechanism=0,
            expected_auth_sampling_priority=2,
            expected_auth_sampling_mechanism=3)

        # Even if subrequest tracing is enabled, auth request delegation
        # requires `allow_delegation_in_subrequests;`.
        self.run_test(
            trace_subrequests=True,  # <----
            allow_delegation_in_subrequests=False,  # <----
            delegate_to_upstream=False,
            delegate_to_auth=True,  # <----
            # "nginx" will make a default sampling decision.
            # "auth" and "upstream" will honor "nginx"'s decision.
            expected_sampling_decider_service=None,
            expected_nginx_sampling_priority=1,
            expected_nginx_sampling_mechanism=0,
            expected_upstream_sampling_priority=1,
            expected_upstream_sampling_mechanism=0,
            expected_auth_sampling_priority=1,
            expected_auth_sampling_mechanism=0)

        self.run_test(
            trace_subrequests=True,  # <----
            allow_delegation_in_subrequests=True,  # <----
            delegate_to_upstream=False,
            delegate_to_auth=True,  # <----
            # "nginx" will delegate to "auth".
            # "upstream" will honor "auth"'s decision as conveyed by "nginx".
            expected_sampling_decider_service='auth',
            expected_nginx_sampling_priority=2,
            expected_nginx_sampling_mechanism=3,
            expected_upstream_sampling_priority=2,
            expected_upstream_sampling_mechanism=3,
            expected_auth_sampling_priority=2,
            expected_auth_sampling_mechanism=3)

    def test_delegate_to_auth_and_upstream(self):
        # If we're configured to delegate to the authentication service
        # _and_ to the upstream, then the authentication service will
        # "win" and delegation will not happen to the upstream.
        self.run_test(
            trace_subrequests=True,
            allow_delegation_in_subrequests=True,
            delegate_to_upstream=True,
            delegate_to_auth=True,
            # "nginx" will delegate to "auth".
            # "upstream" will honor "auth"'s decision as conveyed by "nginx".
            # Notably, "nginx" will not delegate to "upstream".
            expected_sampling_decider_service='auth',
            expected_nginx_sampling_priority=2,
            expected_nginx_sampling_mechanism=3,
            expected_upstream_sampling_priority=2,
            expected_upstream_sampling_mechanism=3,
            expected_auth_sampling_priority=2,
            expected_auth_sampling_mechanism=3)
