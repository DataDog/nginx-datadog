#!/usr/bin/env python3
# Unless explicitly stated otherwise all files in this repository are licensed
# under the Apache 2.0 License. This product includes software developed at
# Datadog (https://www.datadoghq.com/).
#
# Copyright 2024-Present Datadog, Inc.

"""
This script collects and outputs licenses from third-party dependencies in CSV format.

It relies on the following dependencies:
   - cargo-license (<https://github.com/onur/cargo-license>)
   - licensecheck (<https://github.com/FHPythonUtils/LicenseCheck?tab=readme-ov-file#custom-requirementstxt-in-json-format>)

Usage:
======
# To print 3rd deps licenses
./scripts/license.py

# To save
./scripts/license.py > LICENSE-3rdpaty.csv

If what you are looking for is to add the license to the beginning of files, you can
use addlicense (https://github.com/google/addlicense). Example:

addlicense -f NOTICE -ignore "**/*.yml" .
"""

import sys
import glob
import subprocess
import json
import typing
import os

from pathlib import Path
from dataclasses import dataclass

PROJECT_DIR = os.path.dirname(os.path.dirname(os.path.realpath(__file__)))

@dataclass(frozen=True)
class ThirdParty:
    component: str
    origin: str
    license: str
    copyright: str


def run_cmd(cmd: str) -> typing.Any:
    return subprocess.run(cmd, shell=True, capture_output=True, check=True)


def cargo_licenses() -> typing.Dict[str, ThirdParty]:
    """
    Rely on `cargo-license`.
    """
    workspace_cargo = os.path.join(PROJECT_DIR, "Cargo.toml")
    r = run_cmd(
        f"cargo-license -j --manifest-path {workspace_cargo}",
    )
    j = json.loads(r.stdout)

    res = dict()
    for x in j:
        if x["name"] not in res:
            res[x["name"]] = ThirdParty(
                component="vendor", origin=x["name"], license=x["license"], copyright=""
            )

    return res


def python_licenses() -> typing.Dict[str, ThirdParty]:
    """
    Rely on `license-check`.
    """

    def find_requirements_txt(dir: str) -> typing.List[str]:
        """
        Recursively search for 'requirements.txt' files.
        """
        pattern = os.path.join(dir, "**", "requirements.txt")
        return glob.glob(pattern, recursive=True)

    requirements_txt = ";".join(find_requirements_txt(PROJECT_DIR))
    res = dict()
    r = run_cmd(f"licensecheck -u 'requirements:{requirements_txt}' -f json")
    j = json.loads(r.stdout)

    res = dict()
    for x in j["packages"]:
        if x["name"] not in res:
            res[x["name"]] = ThirdParty(
                component="vendor", origin=x["name"], license=x["license"], copyright=""
            )

    return res

def print_csv(deps: typing.Dict[str, ThirdParty]) -> None:
    print("Component,Origin,License,Copyright")
    for x in deps.values():
        print(f"{x.component},{x.origin},{x.license},{x.copyright}")


def main() -> int:
    deps_licenses = cargo_licenses()
    deps_licenses.update(python_licenses())
    print_csv(deps_licenses)

    return 0


if __name__ == "__main__":
    sys.exit(main())
