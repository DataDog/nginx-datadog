"""Boilerplate for test cases"""

from . import orchestration

import os
import time
import unittest

from enum import Enum


def skipUnlessWaf(func):
    """
    Skip test unless the module is built with AppSec.
    """
    return unittest.skipUnless(
        os.environ.get("WAF", "") == "ON", "Requires AppSec enabled")(func)


def skipUnlessRum(func):
    """
    Skip test unless the module is built with RUM.
    """
    return unittest.skipUnless(
        os.environ.get("RUM", "") == "ON", "Requires RUM enabled")(func)


class NginxFlavor(Enum):
    INGRESS_NGINX = 1
    OPENRESTY = 2
    VANILLA = 3
    UNKNOWN = 4

    @staticmethod
    def from_str(label):
        if label in ("ingress-nginx", "ingress_nginx"):
            return NginxFlavor.INGRESS_NGINX
        elif label in ("openresty", "open resty", "open-resty"):
            return NginxFlavor.OPENRESTY
        else:
            return NginxFlavor.UNKNOWN


def nginx_flavor() -> NginxFlavor:
    return NginxFlavor.from_str(os.environ.get("NGINX_FLAVOR", ""))


class TestCase(unittest.TestCase):
    """Provide boilerplate for test cases.

    Test cases derived from this class have an `orch` property that refers to
    the `orchestration` singleton.  It's a convenience to avoid needing to
    indent test cases in a `with orchestration.singleton() as orch:` block.
    """

    durations_seconds = {}

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        # Don't elide error messages produced by `AssertionError`, i.e. avoid
        # "Diff is 1893 characters long. Set self.maxDiff to None to see it."
        self.maxDiff = None

    @classmethod
    def setUpClass(cls):
        super(TestCase, cls).setUpClass()
        waf_value = os.environ.get("WAF", "OFF")
        cls.waf_disabled = (waf_value == "OFF" or waf_value == "FALSE"
                            or waf_value == "0" or waf_value == "N"
                            or waf_value == "n" or waf_value == "No"
                            or waf_value == "NO" or waf_value == "")
        rum_value = os.environ.get("RUM", "OFF")
        cls.rum_disabled = (rum_value == "OFF" or rum_value == "FALSE"
                            or rum_value == "0" or rum_value == "N"
                            or rum_value == "n" or rum_value == "No"
                            or rum_value == "NO" or rum_value == "")

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


setattr(unittest.TestResult, "startTestRun", startTestRun)


def stopTestRun(self):
    """
    https://docs.python.org/3/library/unittest.html#unittest.TestResult.stopTestRun
    Called once after all tests are executed.
    """
    if "TEST_DURATIONS_FILE" in os.environ:
        with open(os.environ["TEST_DURATIONS_FILE"], "w") as file:
            for case, seconds in TestCase.durations_seconds.items():
                print(seconds, case, file=file)

    global_orch_context.__exit__(None, None, None)


setattr(unittest.TestResult, "stopTestRun", stopTestRun)
