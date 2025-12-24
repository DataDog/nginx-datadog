#!/usr/bin/env python3
"""
HTTP/2 client that opens multiple simultaneous connections, each making
multiple concurrent requests to different endpoints.
"""

import ssl
import socket
import time
import sys
import argparse
import threading
from datetime import datetime
from concurrent.futures import ThreadPoolExecutor, as_completed

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


# Thread-local storage for logging
_log_lock = threading.Lock()


def log(msg, conn_id=None):
    prefix = f"[{datetime.now().strftime('%H:%M:%S.%f')[:-3]}]"
    if conn_id is not None:
        prefix += f" [Conn {conn_id}]"
    with _log_lock:
        print(f"{prefix} {msg}", flush=True)


class HTTP2Connection:
    """Manages a single HTTP/2 connection with multiple concurrent streams."""

    def __init__(self, host, port, conn_id, endpoints):
        self.host = host
        self.port = port
        self.conn_id = conn_id
        self.endpoints = endpoints
        self.sock = None
        self.conn = None
        self.lock = threading.Lock()
        self.streams = {}  # stream_id -> {'status': None, 'data': b'', 'complete': False}
        self.goaway_received = False  # Track if we got GOAWAY - don't send anything after

    def connect(self):
        """Establish HTTP/2 connection."""
        log(f"Connecting to {self.host}:{self.port}...", self.conn_id)

        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(30.0)

        context = ssl.create_default_context()
        context.check_hostname = False
        context.verify_mode = ssl.CERT_NONE
        context.set_alpn_protocols(['h2'])

        self.sock = context.wrap_socket(sock, server_hostname=self.host)
        self.sock.connect((self.host, self.port))

        negotiated_protocol = self.sock.selected_alpn_protocol()
        if negotiated_protocol != 'h2':
            raise RuntimeError(f"Expected h2, got {negotiated_protocol}")

        log(f"Connected via {negotiated_protocol}", self.conn_id)

        config = H2Configuration(client_side=True)
        self.conn = H2Connection(config=config)
        self.conn.initiate_connection()
        self.sock.sendall(self.conn.data_to_send())

    def send_request(self, path):
        """Send a single HTTP/2 request, returns stream_id."""
        with self.lock:
            stream_id = self.conn.get_next_available_stream_id()
            headers = [
                (':method', 'GET'),
                (':path', path),
                (':scheme', 'https'),
                (':authority', self.host),
                ('accept-encoding', 'gzip, deflate, br'),
            ]
            self.conn.send_headers(stream_id, headers, end_stream=True)
            self.sock.sendall(self.conn.data_to_send())
            self.streams[stream_id] = {'status': None, 'data': b'', 'complete': False, 'path': path}
            log(f"  -> Stream {stream_id}: GET {path}", self.conn_id)
            return stream_id

    def receive_responses(self, expected_streams):
        """Receive responses for all pending streams."""
        completed = 0
        goaway_received = False

        while completed < expected_streams and not goaway_received:
            try:
                data = self.sock.recv(65536)
            except socket.timeout:
                log("Socket timeout waiting for responses", self.conn_id)
                break
            if not data:
                log("Connection closed by server", self.conn_id)
                break

            with self.lock:
                events = self.conn.receive_data(data)

                for event in events:
                    if isinstance(event, ResponseReceived):
                        stream_id = event.stream_id
                        if stream_id in self.streams:
                            headers_dict = dict(event.headers)
                            status = headers_dict.get(b':status', b'???').decode()
                            self.streams[stream_id]['status'] = status
                            log(f"  <- Stream {stream_id}: Status {status}", self.conn_id)

                    elif isinstance(event, DataReceived):
                        stream_id = event.stream_id
                        if stream_id in self.streams:
                            self.streams[stream_id]['data'] += event.data
                        # Only ACK data if we haven't received GOAWAY
                        if not goaway_received:
                            self.conn.acknowledge_received_data(event.flow_controlled_length, event.stream_id)

                    elif isinstance(event, StreamEnded):
                        stream_id = event.stream_id
                        if stream_id in self.streams:
                            self.streams[stream_id]['complete'] = True
                            data_len = len(self.streams[stream_id]['data'])
                            log(f"  <- Stream {stream_id}: Complete ({data_len} bytes)", self.conn_id)
                            completed += 1

                    elif isinstance(event, StreamReset):
                        stream_id = event.stream_id
                        log(f"  <- Stream {stream_id}: RESET (error_code={event.error_code})", self.conn_id)
                        if stream_id in self.streams:
                            self.streams[stream_id]['complete'] = True
                            completed += 1

                    elif isinstance(event, ConnectionTerminated):
                        log(f"  <- GOAWAY received (error_code={event.error_code}) - NOT acknowledging", self.conn_id)
                        goaway_received = True
                        self.goaway_received = True

                # Only send outgoing data if we haven't received GOAWAY
                if not goaway_received:
                    outgoing = self.conn.data_to_send()
                    if outgoing:
                        self.sock.sendall(outgoing)
                else:
                    # Discard any pending outgoing data (just consume it, don't send)
                    _ = self.conn.data_to_send()

        return completed, goaway_received

    def run_requests(self, num_requests):
        """Send multiple concurrent requests and wait for responses."""
        # Send all requests first (concurrent streams)
        stream_ids = []
        for i in range(num_requests):
            path = self.endpoints[i % len(self.endpoints)]
            stream_id = self.send_request(path)
            stream_ids.append(stream_id)

        # Receive all responses
        completed, goaway = self.receive_responses(num_requests)
        return completed, goaway

    def close(self):
        """Close the connection."""
        if self.goaway_received:
            # Don't send GOAWAY back - just close the socket silently
            log("Closing socket without sending GOAWAY", self.conn_id)
        else:
            # Normal graceful close
            if self.conn:
                try:
                    self.conn.close_connection()
                    self.sock.sendall(self.conn.data_to_send())
                except Exception:
                    pass
        if self.sock:
            self.sock.close()
        log("Connection closed", self.conn_id)


def run_connection(conn_id, host, port, requests_per_conn, endpoints):
    """Worker function to run a single connection with multiple requests."""
    try:
        conn = HTTP2Connection(host, port, conn_id, endpoints)
        conn.connect()
        completed, goaway = conn.run_requests(requests_per_conn)
        conn.close()
        return conn_id, completed, goaway, None
    except Exception as e:
        return conn_id, 0, False, str(e)


def main():
    parser = argparse.ArgumentParser(
        description='HTTP/2 multi-connection load generator',
        formatter_class=argparse.ArgumentDefaultsHelpFormatter
    )
    parser.add_argument('host', nargs='?', default='nginx',
                        help='Target host')
    parser.add_argument('-p', '--port', type=int, default=443,
                        help='Target port')
    parser.add_argument('-t', '--total', type=int, default=10,
                        help='Total number of connections to make')
    parser.add_argument('-c', '--concurrent', type=int, default=5,
                        help='Maximum concurrent connections')
    parser.add_argument('-r', '--requests', type=int, default=5,
                        help='Number of concurrent requests per connection')
    parser.add_argument('-l', '--loop', action='store_true',
                        help='Run continuously in a loop')
    parser.add_argument('-d', '--delay', type=float, default=1.0,
                        help='Delay between loop iterations (seconds)')
    parser.add_argument('-e', '--endpoints', nargs='+',
                        default=['/healthz', '/lua/threads', '/lua/socket', '/json/600', '/get/30/3/50'],
                        help='List of endpoints to request')

    args = parser.parse_args()

    log(f"HTTP/2 Multi-Connection Client")
    log(f"  Target: {args.host}:{args.port}")
    log(f"  Total connections: {args.total}")
    log(f"  Concurrent connections: {args.concurrent}")
    log(f"  Requests per connection: {args.requests}")
    log(f"  Endpoints: {args.endpoints}")
    log("=" * 70)

    iteration = 0
    while True:
        iteration += 1
        if args.loop:
            log(f"=== Iteration {iteration} ===")

        total_completed = 0
        total_goaway = 0
        total_errors = 0

        with ThreadPoolExecutor(max_workers=args.concurrent) as executor:
            futures = {}
            for conn_id in range(args.total):
                future = executor.submit(
                    run_connection,
                    conn_id,
                    args.host,
                    args.port,
                    args.requests,
                    args.endpoints
                )
                futures[future] = conn_id

            for future in as_completed(futures):
                conn_id, completed, goaway, error = future.result()
                if error:
                    log(f"Error: {error}", conn_id)
                    total_errors += 1
                else:
                    total_completed += completed
                    if goaway:
                        total_goaway += 1

        log("-" * 70)
        log(f"Summary: {total_completed} requests completed, {total_goaway} GOAWAYs, {total_errors} errors")
        log("=" * 70)

        if not args.loop:
            break

        time.sleep(args.delay)


if __name__ == "__main__":
    main()
