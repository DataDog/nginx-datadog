"""Regression test for <https://github.com/DataDog/nginx-datadog/issues/479>.

The cache manager and cache loader processes run the module's init process
hook, but exit without running its exit process hook. A tracer started there
crashed the process on exit.
"""

from pathlib import Path

from .. import case


class TestCacheProcesses(case.TestCase):

    def setUp(self):
        super().setUp()
        conf_path = Path(__file__).parent / 'conf/nginx.conf'
        conf_text = conf_path.read_text()
        status, log_lines = self.orch.nginx_replace_config(
            conf_text, conf_path.name)
        self.assertEqual(0, status, log_lines)

    def test_cache_manager_exits_cleanly(self):
        # A reload tells the old cache manager to quit.
        self.orch.reload_nginx()
        exit_line = self.orch.wait_for_log_message(
            'nginx', r'cache manager process \d+ exited', timeout_secs=10)
        self.assertIn('exited with code 0', exit_line)
