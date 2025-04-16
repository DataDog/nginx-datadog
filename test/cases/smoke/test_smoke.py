from .. import case

from pathlib import Path


class TestSmoke(case.TestCase):

    def test_smoke(self):
        conf_path = Path(__file__).parent / "./conf/nginx.conf"
        conf_text = conf_path.read_text()
        status, log_lines = self.orch.nginx_replace_config(
            conf_text, conf_path.name)
        self.assertEqual(status, 0, log_lines)

    def test_minimal_config(self):
        """Verify that an nginx configuration without an "http" configuration
        does not crash on module load, per the bug reported in
        <https://github.com/DataDog/nginx-datadog/pull/17>.
        """
        conf_path = Path(__file__).parent / "./conf/minimal.conf"
        conf_text = conf_path.read_text()
        status, log_lines = self.orch.nginx_replace_config(
            conf_text, conf_path.name)
        self.assertEqual(status, 0, log_lines)
