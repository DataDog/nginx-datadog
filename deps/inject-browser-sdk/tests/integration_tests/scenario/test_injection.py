# Unless explicitly stated otherwise all files in this repository are licensed
# under the Apache 2.0 License. This product includes software developed at
# Datadog (https://www.datadoghq.com/).
#
# Copyright 2024-Present Datadog, Inc.

import os
import pytest
import requests
import gzip
from aiohttp import web

SIMPLE_HTML = """<html>
        <head>
            <title>Upstream</title>
        </head>
        <body>
            <p> Hello, Dog! </p>
        </body>
        </html>
"""


def __assert_rum_injection(headers, body):
    """
    The plugin inject:
       - `x-datadog-sdk-injected` header.
       - HTML RUM SDK scripts.
    """
    assert headers["content-length"] != "0"
    assert headers["x-datadog-rum-injected"] == "1"
    assert "datadog-rum.js" in body.decode()


@pytest.mark.xfail(os.environ.get("WEBSERVER-FLAVOR", "") == "iis", reason="IIS serves static file directly")
@pytest.mark.parametrize("path", ["/index.html", "/large.html"])
def test_injection_static_html_file(path):
    """
    Verify the RUM Browser SDK is injected on:
      - small page (10-20 KiB)
      - large HTML page (1-2 MiB)

    Reason: Some web-servers process requests by chunk, while others
    have access to the full HTML page during the injection.
    This assess by chunk logic is working as intended.
    """

    url = "http://localhost:8080" + path
    r = requests.get(url)
    assert r.status_code == 200
    __assert_rum_injection(r.headers, r.content)


def test_proxy_injection(upstream_server):
    """
    Verify integration injects the RUM Browser SDK on proxified requests.
    Also verify they set `x-datadog-sdk-injection` HTTP Header to signal
    the upstream server to let the caller inject the SDK.
    """

    async def upstream_handler(request):
        request.app["ctx"]["last_request"] = request
        return web.Response(body=SIMPLE_HTML, content_type="text/html")

    upstream_server.add_route("/proxy", upstream_handler)

    r = requests.get("http://localhost:8080/proxy")

    assert r.status_code == 200
    assert (
        upstream_server.get_last_request().headers["x-datadog-rum-injection-pending"]
        == "1"
    )
    __assert_rum_injection(r.headers, r.content)


def test_injection_only_for_html_content(upstream_server):
    """
    Assess the RUM Browser SDK is injected only when the content has the
    HTML content-type.
    """

    async def upstream_handler(_):
        return web.Response(body=SIMPLE_HTML, content_type="text/plain")

    upstream_server.add_route("/content-type", upstream_handler)

    r0 = requests.get("http://localhost:8080/content-type")
    assert r0.status_code == 200

    r1 = requests.get("http://localhost:8081/content-type")
    assert r1.status_code == 200

    assert r0.headers == r1.headers
    assert r0.content == r1.content


def test_no_injection_in_compressed_html(upstream_server):
    """
    Assess the browser SDK is not injected when the HTML content is compressed
    """

    async def upstream_handler(_):
        headers = {"Content-Encoding": "gzip"}
        compressed_body = gzip.compress(SIMPLE_HTML.encode())
        return web.Response(body=compressed_body, headers=headers)

    upstream_server.add_route("/compressed", upstream_handler)

    r0 = requests.get("http://localhost:8080/compressed")
    assert r0.status_code == 200

    r1 = requests.get("http://localhost:8081/compressed")
    assert r1.status_code == 200

    assert r0.headers == r1.headers
    assert r0.content == r1.content


def test_no_injection_header_set(upstream_server):
    """
    Asssess an upstream server can bypass injection
    """
    async def upstream_handler(_):
        headers = {"x-datadog-rum-injected": "1"}
        return web.Response(body=SIMPLE_HTML, headers=headers)

    upstream_server.add_route("/bypass_injection", upstream_handler)

    r0 = requests.get("http://localhost:8080/bypass_injection")
    assert r0.status_code == 200

    r1 = requests.get("http://localhost:8081/bypass_injection")
    assert r1.status_code == 200

    assert r0.headers == r1.headers
    assert r0.content == r1.content


# def test_injection_disabled():
#     """
#     Assess the browser SDK is not injected when the feature is turned off.
#     """
#     pass
#
#
# def test_no_injection_status_code():
#     """
#     Assess the browser SDK is not injected for some status code.
#     Only 4xx at the moment.
#     """
#     pass
#
