#!/usr/bin/env python3
"""
Script executed by the 1-pipeline (internal tool) to prepare
OCI artifacts that will be used by the Datadog Installer.

How does it work?
=================
For a specific nginx-datadog version, it downloads all artifacts from that
release compatible with the architecture and put it in a subfolder per NGINX version

Example
=======

sources
├── 1.27.1
│    └── ngx_http_datadog_module.so
├── 1.27.0
│    └── ngx_http_datadog_module.so
└── version

Limitations: One OCI for all NGINX versions supported.
"""
import argparse
import sys
import urllib.request
import tarfile
import json
import re
import os
import tempfile
import typing


def get_release_data(version: str) -> typing.Any:
    url = f"https://api.github.com/repos/DataDog/nginx-datadog/releases/tags/{version}"

    req = urllib.request.Request(url)

    with urllib.request.urlopen(req) as response:
        if response.status != 200:
            print(f"Error: Failed to fetch release info (HTTP {response.status})")
            return []
        data = response.read().decode("utf-8")
        return json.loads(data)


def download_file(url: str, destination: str) -> None:
    urllib.request.urlretrieve(url, destination)


def extract_tarball(tarball: str, destination: str) -> None:
    with tarfile.open(tarball) as tar:
        tar.extractall(path=destination)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Prepare artifacts for OCI packaging",
    )
    parser.add_argument(
        "--version-tag",
        metavar="VERSION_TAG",
        help="nginx-datadog release version tag",
        required=True,
    )
    parser.add_argument(
        "--arch", help="CPU architecture", choices=["arm64", "amd64"], required=True
    )
    args = parser.parse_args()

    release_data = get_release_data(args.version_tag)
    assets = release_data.get("assets", [])
    if not assets:
        print("No artifacts found for this release.")
        return 1

    artifacts = []
    asset_reg = re.compile(f"ngx_http_datadog_module-{args.arch}-(.*).so.tgz$")
    for asset in assets:
        asset_name = asset.get("name", "")
        matches = asset_reg.match(asset_name)
        if not matches:
            continue

        artifacts.append((matches.group(1), asset["browser_download_url"]))

    if not artifacts:
        print("nothing to download dude")
        return 1

    os.mkdir("sources")

    for nginx_version, artifact_url in artifacts:
        out_dir = os.path.join("sources", nginx_version)

        os.mkdir(out_dir)

        with tempfile.TemporaryDirectory() as work_dir:
            tarball = os.path.join(work_dir, "ngx_http_datadog_module.tgz")
            download_file(artifact_url, tarball)
            extract_tarball(tarball, out_dir)

    return 0


if __name__ == "__main__":
    sys.exit(main())
