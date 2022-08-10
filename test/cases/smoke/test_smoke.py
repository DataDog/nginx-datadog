from .. import case

from pathlib import Path


class TestSmoke(case.TestCase):

    def test_smoke(self):
        conf_path = Path(__file__).parent / './conf/nginx.conf'
        conf_text = conf_path.read_text()
        status, log_lines = self.orch.nginx_replace_config(
            conf_text, conf_path.name)
        self.assertEqual(status, 0, log_lines)
