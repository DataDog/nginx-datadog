"""Entry points shared by make, GitLab, and the examples."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

from .harness import Docker, Images, MODES, ROOT, Sandbox, Workload, unique_uri
from .package import build, module_checksum


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("action",
                        choices=("test", "example-up", "example-down",
                                 "example-request", "example-traces"))
    parser.add_argument("--source",
                        choices=("published", "checkout"),
                        default=os.environ.get("INJECTION_SOURCE",
                                               "published"))
    parser.add_argument("--package",
                        default=os.environ.get("INJECTION_PACKAGE"))
    parser.add_argument("--mode",
                        choices=("docker", *MODES),
                        default=os.environ.get("INJECTION_MODE", "docker"))
    parser.add_argument("--artifacts",
                        default=os.environ.get(
                            "INJECTION_ARTIFACTS",
                            "test/injection/artifacts/latest"))
    args, extra = parser.parse_known_args()
    os.chdir(ROOT)
    docker = Docker(args.artifacts)
    if args.action.startswith("example-") and args.action != "example-up":
        return existing_example(docker, args.action)
    info = json.loads(docker.run("info", "--format", "{{json .}}"))
    arch = {
        "aarch64": "arm64",
        "x86_64": "amd64"
    }.get(info["Architecture"], info["Architecture"])
    package = Path(args.package).resolve(strict=True) if args.package else None
    if not package and args.source == "checkout" and os.environ.get(
            "GITLAB_CI"):
        packages = list((ROOT / "packaging").glob(f"*linux*{arch}*.tar"))
        assert len(
            packages
        ) == 1, f"Expected one package-oci Linux {arch} artifact: {packages}"
        package = packages[0]
    if not package and args.source == "checkout":
        package = build(docker, arch)
    if package:
        checksum = module_checksum(package, arch)
        (docker.artifacts / "input-package.json").write_text(
            json.dumps(
                {
                    "path":
                    str(package),
                    "package_sha256":
                    hashlib.sha256(package.read_bytes()).hexdigest(),
                    "module_sha256":
                    checksum,
                    "architecture":
                    arch
                },
                indent=2))
    if args.action == "example-up":
        example_up(docker, args, package)
        return 0
    command = [
        sys.executable, "-m", "pytest", "-c", "test/pyproject.toml",
        "test/injection", "-v", "--strict-markers", "--injection-artifacts",
        str(docker.artifacts), f"--junitxml={docker.artifacts / 'junit.xml'}"
    ]
    if package:
        command.extend(["--injection-package", str(package)])
    return subprocess.call([*command, *extra])


def example_up(docker, args, package):
    state_path = docker.artifacts / "example.json"
    assert not state_path.exists(), f"Run example-down first: {state_path}"
    images = Images(docker)
    mode = "docker-debian" if args.mode == "docker" else args.mode
    sandbox = Sandbox(images, mode, docker.artifacts / "example", package)
    try:
        sandbox.start()
        sandbox.install()
        environment = {
            key: value
            for key, value in os.environ.items()
            if key in ("DD_SERVICE", "DD_ENV", "DD_VERSION", "DD_TAGS",
                       "DD_TRACE_AGENT_URL", "DD_AGENT_HOST",
                       "DD_TRACE_AGENT_PORT", "DD_TRACE_SAMPLING_RULES",
                       "DD_TRACE_PROPAGATION_STYLE_EXTRACT",
                       "DD_TRACE_PROPAGATION_STYLE_INJECT", "DD_TRACE_ENABLED",
                       "DD_INSTRUMENT_SERVICE_WITH_APM")
        }
        if "DD_AGENT_HOST" in environment and "DD_TRACE_AGENT_URL" not in environment:
            environment["DD_TRACE_AGENT_URL"] = None
        workload = Workload(sandbox, "example", nginx_env=environment).start()
        print(workload.request(unique_uri(True)))
        state_path.write_text(
            json.dumps(
                {
                    "container": sandbox.container,
                    "image": images.host,
                    "mode": mode,
                    "context": docker.prefix
                },
                indent=2))
        print(
            "Example is running. Use make injection-example-request, injection-example-traces, and injection-example-down."
        )
    except BaseException:
        try:
            sandbox.close()
        finally:
            images.close()
        raise


def existing_example(docker, action):
    state_path = docker.artifacts / "example.json"
    state = json.loads(state_path.read_text())
    docker.prefix = state["context"]
    container = state["container"]
    labels = json.loads(
        docker.run("inspect", "--format", "{{json .Config.Labels}}",
                   container))
    assert "com.datadog.nginx-injection" in labels, "Container is not an injection example"
    if action == "example-request":
        print(
            docker.run("exec", container, "curl", "-fsS",
                       f"http://127.0.0.1:8080{unique_uri(True)}"))
    elif action == "example-traces":
        print(
            docker.run("exec", container, "curl", "-fsS",
                       "http://127.0.0.1:8126/test/traces"))
    else:
        images = object.__new__(Images)
        images.docker, images.host = docker, state["image"]
        sandbox = Sandbox(images, state["mode"], docker.artifacts / "example")
        sandbox.container, sandbox.collectors = container, ["a", "b"]
        workload = Workload(sandbox, "example")
        workload.running = True
        workload.containers = [] if state["mode"] == "host" else [
            "backend", "nginx"
        ]
        try:
            sandbox.close()
        finally:
            images.close()
            state_path.unlink()
    return 0


if __name__ == "__main__":
    sys.exit(main())
