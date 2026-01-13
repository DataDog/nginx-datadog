#!/usr/bin/env python3
"""Raw WebSocket upgrade test - checks byte order of HTTP headers vs WebSocket frame"""

import socket
import sys
import base64


def test_ws_raw(host, port, path):
    key = base64.b64encode(b"test-key-1234567").decode()

    request = (f"GET {path} HTTP/1.1\r\n"
               f"Host: {host}:{port}\r\n"
               f"Upgrade: websocket\r\n"
               f"Connection: Upgrade\r\n"
               f"Sec-WebSocket-Key: {key}\r\n"
               f"Sec-WebSocket-Version: 13\r\n"
               f"\r\n")

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(15)

    try:
        sock.connect((host, int(port)))
        sock.sendall(request.encode())

        response = b""
        try:
            while len(response) < 4096:
                chunk = sock.recv(4096)
                if not chunk:
                    break
                response += chunk
                # stop if we have headers and some data after
                if b"\r\n\r\n" in response and len(response) > 200:
                    break
        except socket.timeout:
            pass

        # raw bytes to stdout
        sys.stdout.buffer.write(response)

    finally:
        sock.close()


if __name__ == "__main__":
    if len(sys.argv) != 4:
        print(f"Usage: {sys.argv[0]} <host> <port> <path>", file=sys.stderr)
        sys.exit(1)
    test_ws_raw(sys.argv[1], sys.argv[2], sys.argv[3])
