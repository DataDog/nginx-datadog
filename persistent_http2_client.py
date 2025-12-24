#!/usr/bin/env python3
"""
HTTP/2 client that makes multiple requests over a single persistent connection
to trigger nginx keepalive limits and GOAWAY frames.
"""

import ssl
import socket
import time
import sys
from datetime import datetime

try:
    from h2.connection import H2Connection
    from h2.events import (
        ResponseReceived, DataReceived, StreamEnded, WindowUpdated,
        RemoteSettingsChanged, SettingsAcknowledged, StreamReset,
        ConnectionTerminated
    )
    from h2.config import H2Configuration
except ImportError as e:
    print(f"Error: h2 library not installed. Run: pip install h2 ({e})")
    sys.exit(1)


def log(msg):
    print(f"[{datetime.now().strftime('%H:%M:%S.%f')[:-3]}] {msg}", flush=True)


class HTTP2Client:
    def __init__(self, host, port=443):
        self.host = host
        self.port = port
        self.sock = None
        self.conn = None

    def connect(self):
        """Establish HTTP/2 connection"""
        log(f"Connecting to {self.host}:{self.port}...")

        # Create socket
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(10.0)

        # Wrap with SSL
        context = ssl.create_default_context()
        context.check_hostname = False
        context.verify_mode = ssl.CERT_NONE
        context.set_alpn_protocols(['h2'])

        self.sock = context.wrap_socket(sock, server_hostname=self.host)
        self.sock.connect((self.host, self.port))

        # Verify HTTP/2
        negotiated_protocol = self.sock.selected_alpn_protocol()
        if negotiated_protocol != 'h2':
            raise RuntimeError(f"Expected h2, got {negotiated_protocol}")

        log(f"✓ Connected via {negotiated_protocol}")

        # Initialize H2 connection
        config = H2Configuration(client_side=True)
        self.conn = H2Connection(config=config)
        self.conn.initiate_connection()
        self.sock.sendall(self.conn.data_to_send())

    def make_request(self, path, stream_id=None):
        """Make a single HTTP/2 request on this connection"""
        if stream_id is None:
            stream_id = self.conn.get_next_available_stream_id()

        headers = [
            (':method', 'GET'),
            (':path', path),
            (':scheme', 'https'),
            (':authority', self.host),
            ('accept-encoding', 'gzip, deflate, br'),
        ]

        log(f"  → Stream {stream_id}: GET {path}")
        self.conn.send_headers(stream_id, headers, end_stream=True)
        self.sock.sendall(self.conn.data_to_send())

        # Receive response
        response_complete = False
        response_data = b""
        status = None
        goaway_received = False

        while not response_complete:
            data = self.sock.recv(65536)
            if not data:
                log(f"  ✗ Stream {stream_id}: Connection closed by server")
                break

            events = self.conn.receive_data(data)

            for event in events:
                if isinstance(event, ResponseReceived):
                    headers_dict = dict(event.headers)
                    status = headers_dict.get(b':status', b'???').decode()
                    log(f"  ← Stream {stream_id}: Status {status}")

                elif isinstance(event, DataReceived):
                    response_data += event.data
                    self.conn.acknowledge_received_data(event.flow_controlled_length, event.stream_id)

                elif isinstance(event, StreamEnded):
                    response_complete = True
                    log(f"  ✓ Stream {stream_id}: Complete ({len(response_data)} bytes)")

                # Note: GoAwayReceived doesn't exist in h2 4.3.0
                # GOAWAY is handled internally by the h2 library

                elif isinstance(event, StreamReset):
                    log(f"  ✗ Stream {stream_id}: RESET (error_code={event.error_code})")
                    response_complete = True

                elif isinstance(event, ConnectionTerminated):
                    log(f"  ✗ Connection terminated: error_code={event.error_code}")
                    return False

            # Send any pending data
            outgoing = self.conn.data_to_send()
            if outgoing:
                self.sock.sendall(outgoing)

        return not goaway_received

    def close(self):
        """Close the connection"""
        if self.conn:
            try:
                self.conn.close_connection()
                self.sock.sendall(self.conn.data_to_send())
            except:
                pass
        if self.sock:
            self.sock.close()
        log("Connection closed")


def main():
    host = sys.argv[1] if len(sys.argv) > 1 else "nginx"
    max_requests = int(sys.argv[2]) if len(sys.argv) > 2 else 10

    log(f"Starting persistent HTTP/2 client (max {max_requests} requests per connection)")
    log("=" * 70)

    # Test endpoints
    endpoints = [
        "/healthz",
        "/lua/threads",
        "/lua/socket",
        "/json/600",
        "/get/30/3/50",
    ]

    while True:
        try:
            client = HTTP2Client(host)
            client.connect()

            # Make multiple requests on the same connection
            requests_made = 0
            for i in range(max_requests):
                path = endpoints[i % len(endpoints)]

                log(f"Request {i+1}/{max_requests}")
                success = client.make_request(path)
                requests_made += 1

                if not success:
                    log("⚠ Server indicated GOAWAY - connection will close")
                    break

                # Small delay between requests
                time.sleep(0.1)

            log(f"Completed {requests_made} requests on this connection")
            client.close()

        except Exception as e:
            log(f"Error: {e}")
            import traceback
            traceback.print_exc()

        log("-" * 70)
        time.sleep(2)


if __name__ == "__main__":
    main()
