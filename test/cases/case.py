"""Boilerplate for test cases"""

from . import orchestration

import os
import sys
import time
import unittest


class TestCase(unittest.TestCase):
    """Provide boilerplate for test cases.
    
    Test cases derived from this class have an `orch` property that refers to
    the `orchestration` singleton.  It's a convenience to avoid needing to
    indent test cases in a `with orchestration.singleton() as orch:` block.
    """
    durations_seconds = {}

    def setUp(self):
        context = self.orch_context = orchestration.singleton()
        self.orch = context.__enter__()
        self.begin = time.monotonic()

    def tearDown(self):
        end = time.monotonic()
        self.durations_seconds[self.id()] = end - self.begin

        self.orch_context.__exit__(None, None, None)


# `startTestRun` and `stopTestRun` are injected into the `unittest` module so
# that test suites that span multiple modules share a scoped instance of
# `Orchestration`, i.e. `docker compose up` happens before any tests run,
# and `docker compose down` happens after all tests are finished.
#
# See <https://stackoverflow.com/a/64892396>.
global_orch_context = None


def startTestRun(self):
    """
    https://docs.python.org/3/library/unittest.html#unittest.TestResult.startTestRun
    Called once before any tests are executed.
    """
    global global_orch_context
    global_orch_context = orchestration.singleton()
    global_orch_context.__enter__()


setattr(unittest.TestResult, 'startTestRun', startTestRun)


def stopTestRun(self):
    """
    https://docs.python.org/3/library/unittest.html#unittest.TestResult.stopTestRun
    Called once after all tests are executed.
    """
    if 'TEST_DURATIONS_FILE' in os.environ:
        with open(os.environ['TEST_DURATIONS_FILE'], 'w') as file:
            for case, seconds in TestCase.durations_seconds.items():
                print(seconds, case, file=file)

    global_orch_context.__exit__(None, None, None)


setattr(unittest.TestResult, 'stopTestRun', stopTestRun)
