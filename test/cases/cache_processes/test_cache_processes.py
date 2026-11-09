"""Regression test for <https://github.com/DataDog/nginx-datadog/issues/479>.

The cache manager and cache loader processes run the module's init process
hook, but exit without running its exit process hook. A tracer started there
crashed the process on exit.

On musl, only helpers of a freshly started nginx crashed, so this test
catches the bug on glibc images only.
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

    def test_cache_processes_exit_cleanly(self):
        # A reload tells the old cache manager and cache loader to quit.
        self.orch.reload_nginx()
        exit_lines = [
            self.orch.wait_for_log_message(
                'nginx',
                r'cache (manager|loader) process \d+ exited',
                timeout_secs=10) for _ in range(2)
        ]
        self.assertTrue(any('cache manager' in line for line in exit_lines))
        self.assertTrue(any('cache loader' in line for line in exit_lines))
        for line in exit_lines:
            self.assertIn('exited with code 0', line)
