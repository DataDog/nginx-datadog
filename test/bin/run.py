#!/usr/bin/env python3
"""
Utility script to run integration tests.

Usage:
======
To test nginx-datadog modules:
./run.py --image nginx:1.27.2 --module-path build/ngx_http_datadog_module.so -- --failfast --verbose
"""

import subprocess
import shutil
import argparse
import platform
import shlex
import sys
import os
import time

DRY_RUN = False
PROJECT_DIR = os.path.dirname(
    os.path.dirname(os.path.dirname(os.path.realpath(__file__))))


def run_cmd(cmd: str, **kwargs) -> None:
    print(cmd)
    if not DRY_RUN:
        subprocess.run(cmd, shell=True, check=True, **kwargs)


def run_cmd_with_retries(cmd: str, retries: int = 3, **kwargs) -> None:
    start = time.monotonic()
    for attempt in range(1, retries + 1):
        try:
            run_cmd(cmd, **kwargs)
            return
        except subprocess.CalledProcessError:
            elapsed = time.monotonic() - start
            banner = '!' * 60
            print(f"\n{banner}")
            print(
                f"  RETRY: Attempt {attempt}/{retries} failed after {elapsed:.1f}s"
            )
            if attempt == retries:
                print(f"  BUILD FAILED after {retries} attempts")
                print(f"{banner}\n")
                raise
            print(f"{banner}\n")


def validate_file(arg):
    if not os.path.exists(arg):
        raise argparse.ArgumentTypeError(f"file {arg} does not exist")
    return arg


def format_cmd(cmd: list[str]) -> str:
    return " ".join(shlex.quote(str(part)) for part in cmd)


def normalize_arch(machine: str) -> str:
    if machine in ("arm64", "aarch64"):
        return "aarch64"
    if machine in ("amd64", "x86_64"):
        return "x86_64"
    return machine


def docker_platform_for_arch(arch: str) -> str:
    if arch == "aarch64":
        return "linux/arm64"
    if arch == "x86_64":
        return "linux/amd64"
    raise SystemExit(f"--arch: unsupported architecture {arch!r}")


def build_sanitizer_nginx_base(arch: str, sanitizer: str) -> str:
    """Build an nginx-from-source base image instrumented with the given
    sanitizer ("asan" or "msan"). Returns the Docker image name."""
    nginx_version = os.environ.get("NGINX_VERSION")
    if not nginx_version:
        raise SystemExit(f"--{sanitizer} requires NGINX_VERSION")

    arch = normalize_arch(arch)
    tag = f"nginx-datadog-{sanitizer}-nginx:{nginx_version}-{arch}"
    toolchain_image = os.environ.get("MUSL_TOOLCHAIN_IMAGE",
                                     "nginx_musl_toolchain")
    nginx_service_dir = os.path.join(PROJECT_DIR, "test", "services", "nginx")

    dockerfile = os.path.join(nginx_service_dir,
                              f"Dockerfile.{sanitizer}-base")
    run_cmd_with_retries(
        "docker build "
        f"--platform {shlex.quote(docker_platform_for_arch(arch))} "
        f"--build-arg NGINX_VERSION={shlex.quote(nginx_version)} "
        f"--build-arg ARCH={shlex.quote(arch)} "
        f"--build-arg TOOLCHAIN_IMAGE={shlex.quote(toolchain_image)} "
        f"-f {shlex.quote(dockerfile)} "
        f"-t {shlex.quote(tag)} "
        f"{shlex.quote(nginx_service_dir)}")
    return tag


def run_unittest(inputs: list[str], timeout_seconds: int, cwd: str):
    cmd = ["python3", "-m", "unittest", *inputs]
    print(format_cmd(cmd))
    try:
        subprocess.run(
            cmd,
            cwd=cwd,
            check=True,
            timeout=timeout_seconds if timeout_seconds > 0 else None,
        )
    except subprocess.TimeoutExpired:
        print(f"unittest timed out after {timeout_seconds}s; "
              "bringing down docker compose services")
        subprocess.run(
            ["docker", "compose", "down", "--remove-orphans"],
            cwd=cwd,
            check=False,
        )
        raise


def main() -> int:
    parser = argparse.ArgumentParser(description="Run integration tests")
    parser.add_argument("--image", help="Docker NGINX image under test")
    parser.add_argument(
        "--module-path",
        help="Path of the NGINX module under test",
        required=True,
        type=validate_file,
    )
    parser.add_argument(
        "--asan",
        action="store_true",
        help="Enable ASAN by testing against a toolchain-built nginx image",
    )
    parser.add_argument(
        "--msan",
        action="store_true",
        help="Enable MSAN by testing against a toolchain-built nginx image",
    )
    parser.add_argument(
        "--arch",
        default=os.environ.get("ARCH", platform.machine()),
        help="Target architecture for --asan nginx base image",
    )
    parser.add_argument(
        "--test-timeout",
        type=int,
        default=int(os.environ.get("TEST_TIMEOUT_SECONDS", "0")),
        help="Kill unittest and run docker compose down if tests do not exit "
        "within this many seconds. 0 disables the timeout.",
    )
    parser.add_argument("inputs", nargs="*", help="Positional arguments")

    args = parser.parse_args()

    if args.asan and args.msan:
        parser.error("--asan and --msan are mutually exclusive")
    if args.asan or args.msan:
        if args.image:
            parser.error("--image is not accepted with --asan/--msan; set "
                         "NGINX_VERSION instead")
    elif not args.image:
        parser.error("--image is required unless --asan or --msan is set")

    nginx_service_dir = os.path.join(PROJECT_DIR, "test", "services", "nginx")
    base_image = args.image
    if args.asan:
        base_image = build_sanitizer_nginx_base(args.arch, "asan")
    elif args.msan:
        base_image = build_sanitizer_nginx_base(args.arch, "msan")

    sanitizer = "asan" if args.asan else "msan" if args.msan else "none"
    print(f"     base image : {base_image}")
    print(f"   NGINX module : {args.module_path}")
    print(f"      test args : {args.inputs}")
    print(f"      sanitizer : {sanitizer}")

    shutil.copy(src=args.module_path, dst=nginx_service_dir)

    run_cmd_with_retries(
        f"docker compose build --build-arg BASE_IMAGE={base_image} --parallel --progress=plain",
        cwd=os.path.join(PROJECT_DIR, "test"),
    )

    run_unittest(
        args.inputs,
        timeout_seconds=args.test_timeout,
        cwd=os.path.join(PROJECT_DIR, "test"),
    )

    return 0


if __name__ == "__main__":
    sys.exit(main())
