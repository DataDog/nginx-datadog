"""Follow the logs of a docker container through the Docker Engine API.

The API sends logs in frames. Each frame says which stream it belongs to
(stdout or stderr) and how long it is. So we can rebuild every line exactly,
and keep the order of lines across both streams.

Reading the output of `docker compose up` or `docker logs` does not give this.
These commands write stdout and stderr to two file descriptors. When both point
to the same pipe, a short line of one stream can land inside a long line of the
other.
"""

import http.client
import json
import socket
import struct
import urllib.parse
from collections.abc import Callable, Iterable, Iterator
from typing import Protocol

STREAM_NAMES = {0: "stdin", 1: "stdout", 2: "stderr", 3: "systemerr"}
STDOUT = 1
HEADER_SIZE = 8

# Called with the stream name ("stdout" or "stderr") and the line.
LineCallback = Callable[[str, str], None]


class Readable(Protocol):

    def read(self, size: int, /) -> bytes:
        ...


def follow_container_logs(host: str, container: str,
                          on_line: LineCallback) -> None:
    """Call `on_line(stream, line)` for each line logged by `container`, from
    its start until it stops. `host` is the address of the Docker Engine API,
    e.g. "unix:///var/run/docker.sock". `stream` is "stdout" or "stderr".
    `line` has no trailing newline.
    """
    connection = connect(host)
    try:
        container_path = f"/containers/{urllib.parse.quote(container)}"
        inspection = json.loads(
            get(connection, f"{container_path}/json").read())
        response = get(connection,
                       f"{container_path}/logs?follow=1&stdout=1&stderr=1")
        chunks: Iterable[tuple[int, bytes]]
        if inspection["Config"]["Tty"]:
            # With a TTY, there is a single stream and no frames.
            chunks = ((STDOUT, chunk) for chunk in iter(response.read1, b""))
        else:
            chunks = read_multiplexed(response)
        split_lines(chunks, on_line)
    finally:
        connection.close()


def split_lines(chunks: Iterable[tuple[int, bytes]],
                on_line: LineCallback) -> None:
    """Call `on_line(stream, line)` for each line in `chunks`, an iterable of
    `(stream, data)` pairs.
    """
    pending: dict[int, list[bytes]] = {}  # pieces of unterminated lines
    for stream, data in chunks:
        pieces = data.split(b"\n")
        unterminated = pending.setdefault(stream, [])
        for piece in pieces[:-1]:
            unterminated.append(piece)
            emit_line(on_line, stream, b"".join(unterminated))
            unterminated.clear()
        unterminated.append(pieces[-1])

    for stream, unterminated in pending.items():
        line = b"".join(unterminated)
        if line:
            emit_line(on_line, stream, line)


def emit_line(on_line: LineCallback, stream: int, line: bytes) -> None:
    on_line(STREAM_NAMES.get(stream, str(stream)),
            line.decode("utf8", errors="replace"))


def read_multiplexed(response: Readable) -> Iterator[tuple[int, bytes]]:
    """Yield `(stream, data)` for each frame of a multiplexed stream.

    Each frame has an 8-byte header: the stream number, three zero bytes and
    the length of the data as a big-endian 32-bit integer.
    """
    while True:
        header = read_up_to(response, HEADER_SIZE)
        if not header:
            return
        check_complete(header, HEADER_SIZE)
        stream, size = struct.unpack(">BxxxL", header)
        data = read_up_to(response, size)
        check_complete(data, size)
        yield stream, data


def read_up_to(response: Readable, size: int) -> bytes:
    """Read `size` bytes, or fewer if the stream ends."""
    data = b""
    while len(data) < size:
        chunk = response.read(size - len(data))
        if not chunk:
            break
        data += chunk
    return data


def check_complete(data: bytes, size: int) -> None:
    if len(data) < size:
        raise EOFError(
            f"Docker log stream ended inside a frame: got {len(data)} of "
            f"{size} bytes")


def connect(host: str) -> http.client.HTTPConnection:
    url = urllib.parse.urlsplit(host)
    if url.scheme == "unix":
        return UnixHTTPConnection(url.path)
    if url.scheme == "tcp" and url.hostname:
        return http.client.HTTPConnection(url.hostname, url.port or 2375)
    raise ValueError(f"Unsupported Docker host: {host}")


def get(connection: http.client.HTTPConnection,
        path: str) -> http.client.HTTPResponse:
    connection.request("GET", path)
    response = connection.getresponse()
    if response.status != 200:
        raise Exception(f"Docker API GET {path} returned {response.status}: "
                        f"{response.read()!r}")
    return response


class UnixHTTPConnection(http.client.HTTPConnection):

    def __init__(self, socket_path: str) -> None:
        super().__init__("localhost")
        self.socket_path = socket_path

    def connect(self) -> None:
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.connect(self.socket_path)
