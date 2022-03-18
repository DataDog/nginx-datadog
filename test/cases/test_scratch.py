import case

import os
from pprint import pprint
import sys
import time
import unittest
import urllib.request


class TestScratch(case.TestCase):
    @unittest.skip('this is just scratch code')
    def test_up(self):
        sys.stdout.write('sleeping...')
        seconds = int(os.environ.get('TEST_SECONDS', 0))
        for i in range(seconds, 0, -1):
            sys.stdout.write(f' {i}...')
            sys.stdout.flush()
            time.sleep(1)
        sys.stdout.write('\n')
        sys.stdout.flush()

        print('about to attempt sync')
        log_lines = self.orch.sync_service('agent')
        print('got log lines from sync: ')
        pprint(log_lines)

        # Send a few requests to nginx and sync again.
        for _ in range(3):
            status = self.orch.send_nginx_request('/')
            self.assertEqual(status, 200)

        print('reloading nginx and waiting for old workers to terminate')
        self.orch.reload_nginx(wait_for_workers_to_terminate=True)

        print('about to attempt another sync')
        log_lines = self.orch.sync_service('agent')
        print('got log lines from second sync: ')
        pprint(log_lines)

        # self.assertEqual(1, 2)
        # self.assertEqual(1, 1)
