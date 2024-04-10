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

    def wait_for_req_with_version(self, exp_version, timeout_secs):
        line = self.orch.wait_for_log_message('agent', f'Remote config request with version {exp_version}.*',
                                              timeout_secs=timeout_secs)
        return json.loads(line.split(': ')[1])

    @staticmethod
    def generate_resp(spec):
        """Generates a remote config response with the files in spec, specified as config key => content"""

        version = int(time.time())

        data = {
            "client_configs": list(spec.keys()),
            "roots": [],
            "target_files": [
                {
                    "path": key,
                    "raw": base64.b64encode(content.encode('utf-8')).decode('iso-8859-1'),
                }
                for key, content in spec.items()
            ],
            "targets": base64.b64encode(json.dumps({
                "signatures": [],
                'signed': {
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
                                    "sha256": hashlib.sha256(content.encode('utf-8')).digest().hex()
                                }
                            }
                            for key, content in spec.items()
                        }
                    }
            }).encode('utf-8')).decode('iso-8859-1')
        }
        return json.dumps(data), version

    def apply_cfg(self, spec):
        payload, version = TestSecConfig.generate_resp(spec)
        status, _, _ = self.orch.setup_remote_config_payload(payload)
        self.assertEqual(200, status)
        return version

    def drop_cfg(self):
        version = self.apply_cfg({})
        self.wait_for_req_with_version(version, 15)

    def test_remote_activation(self):
        version = self.apply_cfg({
            'datadog/2/ASM_FEATURES/asm_features_activation/config':
                '{"asm":{"enabled":true}}'
        })
        rem_cfg_req = self.wait_for_req_with_version(version, 15)
        state = next((el for el in rem_cfg_req['client']['state']['config_states']
                      if el['id'] == 'asm_features_activation'))
        self.assertEqual(state['apply_state'], 2)

        code, _, _ = self.orch.send_nginx_http_request('/', headers={'User-agent': 'dd-test-scanner-log-block'})
        self.assertEqual(403, code)

        # Now drop the configuration
        self.drop_cfg()

        code, _, _ = self.orch.send_nginx_http_request('/', headers={'User-agent': 'dd-test-scanner-log-block'})
        self.assertEqual(200, code)

    def test_waf_data(self):
        version = self.apply_cfg({
            'datadog/2/ASM_FEATURES/asm_features_activation/config':
                '{"asm":{"enabled":true}}',
            'datadog/2/ASM_DATA/mydata/config':
                json.dumps({
                    "rules_data": [
                        {
                            "id": "blocked_ips",
                            "type": "ip_with_expiration",
                            "data": [
                                {
                                    "expiration": 0,
                                    "value": "1.2.3.0/24"
                                }
                            ]
                        }
                    ]
                })
        })
        rem_cfg_req = self.wait_for_req_with_version(version, 15)
        state = next((el for el in rem_cfg_req['client']['state']['config_states']
                      if el['id'] == 'mydata'))
        self.assertEqual(state['apply_state'], 2)

        code, _, _ = self.orch.send_nginx_http_request('/', headers={'X-real-ip': '1.2.3.100'})
        self.assertEqual(403, code)

        code, _, _ = self.orch.send_nginx_http_request('/', headers={'X-real-ip': '1.2.4.1'})
        self.assertEqual(200, code)

        self.drop_cfg()

        code, _, _ = self.orch.send_nginx_http_request('/', headers={'X-real-ip': '1.2.3.100'})
        self.assertEqual(200, code)

    def test_asm_dd(self):
        version = self.apply_cfg({
            'datadog/2/ASM_FEATURES/asm_features_activation/config':
                '{"asm":{"enabled":true}}',
            'datadog/2/ASM_DD/full_cfg/config':
                json.dumps({
                    "version": "2.1",
                    "rules": [
                        {
                            "id": "partial_match_values",
                            "name": "Partially match values",
                            "tags": {
                                "type": "security_scanner",
                                "category": "attack_attempt"
                            },
                            "conditions": [
                                {
                                    "parameters": {
                                        "inputs": [
                                            {
                                                "address": "server.request.query"
                                            }
                                        ],
                                        "regex": ".*matched.+value.*"
                                    },
                                    "operator": "match_regex"
                                }
                            ],
                            "transformers": [
                                "values_only"
                            ]
                        }
                    ]
                })
        })

