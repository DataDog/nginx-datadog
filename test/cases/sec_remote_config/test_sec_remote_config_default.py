import base64
import hashlib
import json
import time
from pathlib import Path

from .. import case


class TestSecConfig(case.TestCase):
    """Test with remote activation and remote rules"""

    def setUp(self):
        super().setUp()
        conf_path = Path(__file__).parent / f'./conf/http_default.conf'
        conf_text = conf_path.read_text()
        status, log_lines = self.orch.nginx_replace_config(
            conf_text, conf_path.name)
        self.assertEqual(0, status, log_lines)

    def generate_resp(self, spec):
        """Generates a remote config response with the files in spec, specified as config key => content"""

        data = {
            "client_configs": spec.keys(),
            "roots": [],
            "target_files": [
                {
                    "path": key,
                    "raw": base64.encode(content),
                }
                for key, content in spec.items()
            ],
            "targets": {
                "signatures": [],
                'signed': base64.encode(
                    json.dumps({
                        "_type": "targets",
                        "expires": "2034-8-08T18:08:04Z",
                        "spec_version": "1.0.0",
                        "version": int(time.time()),
                        "custom": {
                            "agent_refresh_interval": 1,
                            "opaque_backend_state": "<value of opaque backend state>"
                        },
                        "targets": {
                            key: {
                                "custom": {
                                    "v": int(time.time())
                                },
                                "length": len(content),
                                "hashes": {
                                    "sha256": base64.encode(hashlib.sha256(content).digest())
                                }
                            }
                            for key, content in spec.items()
                        }
                    })
                )
            },
        }
        json.dumps(data)

    def apply_cfg(self, spec):


    def test_remote_activation(self):

