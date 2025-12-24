#!/usr/bin/env python3
import requests
from urllib3.exceptions import InsecureRequestWarning
requests.packages.urllib3.disable_warnings(InsecureRequestWarning)

BASE_URL = "https://localhost"

def test_endpoint(method, path, **kwargs):
    url = f"{BASE_URL}{path}"
    try:
        r = requests.request(method, url, verify=False, timeout=10, **kwargs)
        status = "✓" if r.status_code < 400 else "✗"
        print(f"{status} {method:4} {path:30} -> {r.status_code}", flush=True)
        return r.status_code < 400
    except Exception as e:
        print(f"✗ {method:4} {path:30} -> ERROR: {str(e)[:50]}", flush=True)
        return False

def main():
    tests = [
        ("GET", "/get/30/3/50", {}),
        ("POST", "/post/25/2/50/10", {"json": {"test": "data"}, "headers": {"Content-Type": "application/json"}}),
        ("GET", "/json/600", {}),
        ("GET", "/lua/socket", {}),
        ("GET", "/lua/threads", {}),
        ("GET", "/lua/timer", {}),
        ("GET", "/lua/subrequest", {}),
        ("POST", "/post/20/3/100/5", {"data": "Lorem ipsum dolor sit amet"}),
        ("GET", "/auth", {"headers": {"Authorization": "mysecret"}}),
    ]

    passed = sum(test_endpoint(method, path, **kwargs) for method, path, kwargs in tests)
    print(f"\n{passed}/{len(tests)} tests passed")
    return passed == len(tests)

if __name__ == "__main__":
    exit(0 if main() else 1)
