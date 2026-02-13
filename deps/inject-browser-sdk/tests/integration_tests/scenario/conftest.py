# Unless explicitly stated otherwise all files in this repository are licensed
# under the Apache 2.0 License. This product includes software developed at
# Datadog (https://www.datadoghq.com/).
#
# Copyright 2024-Present Datadog, Inc.

import pytest

from queue import Queue
from aiohttp import web
import asyncio
import threading


class AioHTTPServer:
    def __init__(self, app, host, port) -> None:
        self._thread = None
        self._stop = asyncio.Event()
        self._host = host
        self._port = port
        self._app = app
        self.loop = asyncio.new_event_loop()

        self._app["ctx"] = {"last_request": None}
        asyncio.set_event_loop(self.loop)

    def internal_run(self) -> None:
        runner = web.AppRunner(self._app)
        self.loop.run_until_complete(runner.setup())
        site = web.TCPSite(runner, self._host, self._port)
        self.loop.run_until_complete(site.start())
        self.loop.run_until_complete(self._stop.wait())
        self.loop.run_until_complete(self._app.cleanup())
        self.loop.close()

    def run(self) -> None:
        self._thread = threading.Thread(target=self.internal_run)
        self._thread.start()

    def stop(self) -> None:
        self.loop.call_soon_threadsafe(self._stop.set)

    def get_last_request(self):
        return self._app["ctx"].get("last_request", None)

    # TODO: Register a wildcard path and register a custom handler
    # that is mutable
    def add_route(self, path: str, handler):
        self._app.router._frozen = False
        self._app.add_routes([web.get(path, handler)])
        self._app.router._frozen = True


def make_temporary_http_server(host: str, port: int):
    app = web.Application()

    # routes = [web.get(*x) for x in handlers]
    # app.add_routes(routes)

    return AioHTTPServer(app, host, port)


def pytest_sessionstart(session: pytest.Session) -> None:
    """
    Called after the Session object has been created and
    before performing collection and entering the run test loop.
    """
    session.config.upstream_server = make_temporary_http_server(
        host="localhost", port=8090
    )
    session.config.upstream_server.run()


def pytest_sessionfinish(session: pytest.Session, exitstatus: int) -> None:
    """
    Called after whole test run finished, right before
    returning the exit status to the system.
    """
    try:
        session.config.upstream_server.stop()
    except:
        pass


@pytest.fixture
def upstream_server(request):
    return request.config.upstream_server
