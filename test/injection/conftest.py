import os
from pathlib import Path
import re

import pytest

from .harness import (Docker, Images, MODES, Sandbox, STABLE_CONFIG_PATHS,
                      SwarmWorkload, Workload)


def pytest_addoption(parser):
    group = parser.getgroup("injection")
    group.addoption("--injection-mode", choices=MODES, action="append")
    group.addoption("--injection-package",
                    default=os.environ.get("INJECTION_PACKAGE"))
    group.addoption("--injection-artifacts",
                    default=os.environ.get("INJECTION_ARTIFACTS",
                                           "test/injection/artifacts/latest"))


def pytest_configure(config):
    if os.environ.get("PYTEST_XDIST_WORKER") or getattr(
            config.option, "numprocesses", 0):
        raise pytest.UsageError("Injection acceptance tests must run serially")


def pytest_generate_tests(metafunc):
    if "mode" in metafunc.fixturenames:
        explicitly_parameterized = any(
            "mode" in marker.args[0].replace(",", " ").split()
            for marker in metafunc.definition.iter_markers("parametrize")
            if marker.args and isinstance(marker.args[0], str))
        if explicitly_parameterized:
            return
        metafunc.parametrize("mode",
                             metafunc.config.getoption("--injection-mode")
                             or MODES,
                             scope="session")


@pytest.hookimpl(trylast=True)
def pytest_collection_modifyitems(config, items):
    selected_modes = config.getoption("--injection-mode")
    deselected = []
    selected = []
    for item in items:
        mode = item.callspec.params.get("mode") if hasattr(
            item, "callspec") else None
        if selected_modes and mode and mode not in selected_modes:
            deselected.append(item)
        else:
            selected.append(item)
    if deselected:
        config.hook.pytest_deselected(items=deselected)
    items[:] = selected
    items.sort(
        key=lambda item: MODES.index(item.callspec.params["mode"]) if hasattr(
            item, "callspec") and "mode" in item.callspec.params else -1)


def pytest_sessionfinish(session, exitstatus):
    reporter = session.config.pluginmanager.getplugin("terminalreporter")
    if reporter and reporter.stats.get("skipped"):
        session.exitstatus = pytest.ExitCode.TESTS_FAILED
    if reporter and os.environ.get("GITLAB_CI") and reporter.stats.get(
            "deselected"):
        session.exitstatus = pytest.ExitCode.TESTS_FAILED


@pytest.fixture(scope="session")
def images(request):
    docker = Docker(request.config.getoption("--injection-artifacts"))
    built = Images(docker)
    try:
        yield built
    finally:
        built.close()


@pytest.fixture(scope="session")
def sandbox_factory(images, request):
    package = request.config.getoption("--injection-package")
    if package:
        package = str(Path(package).resolve(strict=True))

    def create(mode, name):
        return Sandbox(images, mode, images.docker.artifacts / name, package)

    return create


@pytest.fixture(scope="session")
def sandbox(sandbox_factory, mode):
    instance = sandbox_factory(mode, mode)
    try:
        instance.start()
        instance.install()
        yield instance
    finally:
        instance.close()


@pytest.fixture
def workload(sandbox, request):
    name = re.sub(r"[^a-zA-Z0-9_.-]", "_", request.node.name)
    created = []

    def start(nginx_env=None, backend_env=None, image=None):
        instance = Workload(sandbox, name, nginx_env, backend_env, image)
        created.append(instance)
        return instance.start()

    try:
        yield start
    finally:
        for instance in created:
            instance.finish()


@pytest.fixture
def swarm_workload(sandbox, request):
    name = re.sub(r"[^a-zA-Z0-9_.-]", "_", request.node.name)
    created = []

    def start(nginx_env=None):
        instance = SwarmWorkload(sandbox, name, nginx_env)
        created.append(instance)
        return instance.start()

    try:
        yield start
    finally:
        for instance in created:
            instance.finish()


@pytest.fixture
def stable_config(sandbox):
    written = set()

    def write(path, configuration):
        assert path in STABLE_CONFIG_PATHS
        written.add(path)
        return sandbox.write_stable_config(path, configuration)

    try:
        yield write
    finally:
        for path in written:
            sandbox.host("rm", "-f", path)
