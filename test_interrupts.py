#!/usr/bin/env python3
"""Test client connection interruptions with HTTP/1.1 and HTTP/2"""

import socket
import ssl
import time
import sys
import subprocess
from datetime import datetime

def log(msg):
    print(f"[{datetime.now().strftime('%H:%M:%S')}] {msg}", flush=True)

def test_http1_interrupt_request_body(host):
    """HTTP/1.1: Interrupt while sending request body"""
    log("Test: HTTP/1.1 interrupt during request body")
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(2.0)
        context = ssl.create_default_context()
        context.check_hostname = False
        context.verify_mode = ssl.CERT_NONE
        ssl_sock = context.wrap_socket(sock)
        ssl_sock.connect((host, 443))

        request = b"POST /post/25/2/50/10 HTTP/1.1\r\n"
        request += b"Host: " + host.encode() + b"\r\n"
        request += b"Content-Length: 10000\r\n"
        request += b"Content-Type: application/json\r\n\r\n"
        ssl_sock.sendall(request)

        partial_body = b'{"data":"' + (b'x' * 100)
        ssl_sock.sendall(partial_body)
        time.sleep(0.01)
        ssl_sock.close()
        log("  -> Closed during request body")
    except Exception as e:
        log(f"  -> Exception: {e}")

def test_http1_interrupt_response_body(host):
    """HTTP/1.1: Interrupt while receiving response body"""
    log("Test: HTTP/1.1 interrupt during response body")
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(2.0)
        context = ssl.create_default_context()
        context.check_hostname = False
        context.verify_mode = ssl.CERT_NONE
        ssl_sock = context.wrap_socket(sock)
        ssl_sock.connect((host, 443))

        request = b"GET /get/50/5/100 HTTP/1.1\r\n"
        request += b"Host: " + host.encode() + b"\r\n"
        request += b"Accept-Encoding: gzip\r\n\r\n"
        ssl_sock.sendall(request)

        data = b""
        while b"\r\n\r\n" not in data:
            chunk = ssl_sock.recv(1024)
            if not chunk:
                break
            data += chunk

        ssl_sock.recv(100)
        time.sleep(0.01)
        ssl_sock.close()
        log("  -> Closed during response body")
    except Exception as e:
        log(f"  -> Exception: {e}")

def test_http2_interrupt_request_body(host):
    """HTTP/2: Interrupt while sending request body"""
    log("Test: HTTP/2 interrupt during request body")
    try:
        proc = subprocess.Popen(
            ['curl', '-k', '--http2', '-X', 'POST',
             f'https://{host}/post/25/2/50/10',
             '-H', 'Content-Type: application/json',
             '--data-binary', '@/dev/zero',
             '-H', 'Content-Length: 100000'],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL
        )
        time.sleep(0.05)
        proc.kill()
        proc.wait()
        log("  -> Killed curl during upload")
    except Exception as e:
        log(f"  -> Exception: {e}")

def test_http2_interrupt_response_body(host):
    """HTTP/2: Interrupt while receiving response body"""
    log("Test: HTTP/2 interrupt during response body")
    try:
        proc = subprocess.Popen(
            ['curl', '-k', '--http2',
             f'https://{host}/get/100/10/50',
             '-H', 'Accept-Encoding: br, gzip'],
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL
        )
        time.sleep(0.1)
        proc.kill()
        proc.wait()
        log("  -> Killed curl during download")
    except Exception as e:
        log(f"  -> Exception: {e}")

def test_http1_connect_and_close(host):
    """HTTP/1.1: Connect then immediately close"""
    log("Test: HTTP/1.1 connect and immediate close")
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(2.0)
        context = ssl.create_default_context()
        context.check_hostname = False
        context.verify_mode = ssl.CERT_NONE
        ssl_sock = context.wrap_socket(sock)
        ssl_sock.connect((host, 443))
        time.sleep(0.001)
        ssl_sock.close()
        log("  -> Closed immediately after connect")
    except Exception as e:
        log(f"  -> Exception: {e}")

def test_http1_partial_headers(host):
    """HTTP/1.1: Send partial headers then close"""
    log("Test: HTTP/1.1 partial headers then close")
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.settimeout(2.0)
        context = ssl.create_default_context()
        context.check_hostname = False
        context.verify_mode = ssl.CERT_NONE
        ssl_sock = context.wrap_socket(sock)
        ssl_sock.connect((host, 443))
        ssl_sock.sendall(b"GET /get/30")
        time.sleep(0.01)
        ssl_sock.close()
        log("  -> Closed with partial headers")
    except Exception as e:
        log(f"  -> Exception: {e}")

def main():
    host = sys.argv[1] if len(sys.argv) > 1 else "nginx"

    log(f"Starting connection interruption tests against {host}")
    log("=" * 60)

    tests = [
        test_http1_connect_and_close,
        test_http1_partial_headers,
        test_http1_interrupt_request_body,
        test_http1_interrupt_response_body,
        test_http2_interrupt_request_body,
        test_http2_interrupt_response_body,
    ]

    while True:
        for test in tests:
            try:
                test(host)
            except Exception as e:
                log(f"Test failed: {e}")
            time.sleep(2)

        log("-" * 60)
        time.sleep(5)

if __name__ == "__main__":
    main()
