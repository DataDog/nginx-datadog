import json
from pathlib import Path

from .. import case


class TestSecTelemetry(case.TestCase):
    """Test the AppSec state reported in the app-started telemetry event"""

    requires_waf = True

    def appsec_product_at_startup(self, conf_name):
        # Drop telemetry lines left by earlier tests.
        self.orch.sync_service('agent')

        conf_path = Path(__file__).parent / 'conf' / conf_name
        status, log_lines = self.orch.nginx_replace_config(
            conf_path.read_text(), conf_path.name)
        self.assertEqual(0, status, log_lines)

        line = self.orch.wait_for_log_message(
            'agent', 'Telemetry request: .*"app-started"', timeout_secs=30)
        batch = json.loads(line.split(': ', 1)[1])
        app_started = next(msg for msg in batch['payload']
                           if msg['request_type'] == 'app-started')
        return app_started['payload']['products']['appsec']

    def test_appsec_enabled(self):
        appsec = self.appsec_product_at_startup('appsec_on.conf')
        self.assertTrue(appsec['enabled'])

    def test_appsec_disabled(self):
        appsec = self.appsec_product_at_startup('appsec_off.conf')
        self.assertFalse(appsec['enabled'])

    def test_appsec_waiting_for_remote_config(self):
        appsec = self.appsec_product_at_startup('appsec_remote.conf')
        self.assertFalse(appsec['enabled'])
