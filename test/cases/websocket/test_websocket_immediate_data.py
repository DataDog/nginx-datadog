"""Test for WebSocket immediate data bug

This test reproduces a bug when the backend sends data immediately after
the WebSocket upgrade completes. With AppSec enabled, the websocket data
would come before the response header.
"""

import subprocess
from pathlib import Path
from .. import case
from ..orchestration import docker_compose_command, child_env


class TestWebSocketImmediateData(case.TestCase):
    """Test WebSocket with backend sending immediate data after upgrade"""

    requires_waf = True

    def setUp(self):
        super().setUp()

        conf_path = Path(__file__).parent / './conf/http.conf'
        conf_text = conf_path.read_text()

        status, log_lines = self.orch.nginx_replace_config(
            conf_text, conf_path.name)
        self.assertEqual(0, status, log_lines)

        # Consume any previous logging from the agent
        self.orch.sync_service('agent')

    def test_http_headers_before_websocket_frame(self):
        """Test that HTTP 101 headers come before WebSocket frame data."""

        immediate_message = "HELLO_IMMEDIATE"

        script_path = Path(__file__).parent / 'ws_raw_test.py'
        script_content = script_path.read_text()

        copy_cmd = docker_compose_command(
            "exec", "-T", "--", "client", "sh", "-c",
            f"cat > /tmp/ws_raw_test.py << 'EOFSCRIPT'\n{script_content}\nEOFSCRIPT"
        )
        subprocess.run(copy_cmd, env=child_env(), check=True)

        command = docker_compose_command(
            "exec", "-T", "--", "client", "python3", "/tmp/ws_raw_test.py",
            "nginx", "80", f"/ws?immediate_raw={immediate_message}")

        result = subprocess.run(
            command,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            env=child_env(),
            timeout=30,
        )

        if result.stderr:
            print(f"stderr: {result.stderr.decode()}")
        print(f"return code: {result.returncode}")

        response_bytes = result.stdout

        http_pos = response_bytes.find(b"HTTP/1.1 101")
        # WebSocket text frame starts with 0x81 (FIN + opcode 1)
        ws_frame_pos = response_bytes.find(b"\x81")

        print(f"Response length: {len(response_bytes)} bytes")
        print(f"HTTP/1.1 101 position: {http_pos}")
        print(f"WebSocket frame (0x81) position: {ws_frame_pos}")
        print(f"First 200 bytes (hex): {response_bytes[:200].hex()}")
        print(f"First 200 bytes (repr): {repr(response_bytes[:200])}")

        self.assertNotEqual(
            http_pos, -1, f"HTTP/1.1 101 not found in response. "
            f"Response: {response_bytes[:500]}")

        self.assertNotEqual(
            ws_frame_pos, -1, f"WebSocket frame (0x81) not found in response. "
            f"Response: {response_bytes[:500]}")

        # HTTP headers must come BEFORE WebSocket frame data
        self.assertLess(
            http_pos, ws_frame_pos,
            f"BUG: WebSocket frame data (pos {ws_frame_pos}) appeared "
            f"BEFORE HTTP headers (pos {http_pos}). "
            f"This breaks the WebSocket handshake. "
            f"First 200 bytes: {response_bytes[:200].hex()}")
