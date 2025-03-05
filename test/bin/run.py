#!/usr/bin/env python3
"""
Utility script to run integration tests.

Usage:
======
To release nginx-datadog modules:
./run.py --platform linux/amd64 --image nginx:1.27.2 --module-path build/ngx_http_datadog_module.so "--failfast --verbose"
"""

import subprocess
import shutil
import argparse
import sys
import os

DRY_RUN = False
PROJECT_DIR = os.path.dirname(
    os.path.dirname(os.path.dirname(os.path.realpath(__file__))))


def run_cmd(cmd: str, *args, **kwargs) -> None:
    print(cmd)
    if not DRY_RUN:
        subprocess.run(cmd, shell=True, check=True, *args, **kwargs)


def validate_file(arg):
    if not os.path.exists(arg):
        raise argparse.ArgumentTypeError(f"file {arg} does not exist")
    return arg


def main() -> int:
    parser = argparse.ArgumentParser(description="Run integration tests")
    parser.add_argument(
        "--platform",
        help="Docker platform",
        choices=["linux/amd64", "linux/arm64"],
        required=True,
    )
    parser.add_argument("--image",
                        help="Docker NGINX image under tests",
                        required=True)
    parser.add_argument(
        "--module-path",
        help="Path of the NGINX module under test",
        required=True,
        type=validate_file,
    )
    parser.add_argument("inputs", nargs="*", help="Positional arguments")

    args = parser.parse_args()

    print(f"       platform : {args.platform}")
    print(f"     base image : {args.image}")
    print(f"   NGINX module : {args.module_path}")
    print(f"      test args : {args.inputs}")

    shutil.copy(src=args.module_path,
                dst=os.path.join(PROJECT_DIR, "test", "services", "nginx"))

    run_cmd(
        f"docker compose build --build-arg BASE_IMAGE={args.image} --parallel --progress=plain",
        cwd=os.path.join(PROJECT_DIR, "test"),
    )

    if args.inputs:
        run_cmd(
            "python3 -m unittest {0}".format(" ".join(args.inputs)),
            cwd=os.path.join(PROJECT_DIR, "test"),
        )
    else:
        run_cmd("python3 -m unittest", cwd=os.path.join(PROJECT_DIR, "test"))

    return 0


if __name__ == "__main__":
    sys.exit(main())
