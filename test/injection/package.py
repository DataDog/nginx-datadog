"""Use the same RUM module layout and OCI packager as package-oci."""

import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import subprocess
import tarfile
import tempfile

import yaml
import zstandard

from .harness import LABEL, NGINX_VERSION, ROOT

PACKAGER = "registry.ddbuild.io/ci/libdatadog-build/packaging:135928035"


def module_checksum(archive, arch):
    with tarfile.open(archive) as package:
        members = {
            member.name.removeprefix("./"): member
            for member in package
        }
        for path, member in members.items():
            assert not PurePosixPath(
                path).is_absolute() and ".." not in PurePosixPath(path).parts
            assert member.isfile() or member.isdir(
            ), f"Invalid OCI archive entry: {path}"

        def read(path):
            return package.extractfile(members[path]).read()

        def blob(descriptor):
            algorithm, digest = descriptor["digest"].split(":")
            assert algorithm == "sha256"
            data = read(f"blobs/{algorithm}/{digest}")
            assert hashlib.sha256(data).hexdigest() == digest
            return data

        index = json.loads(read("index.json"))
        manifests = [
            entry for entry in index["manifests"]
            if entry.get("platform", {}).get("os") == "linux"
            and entry.get("platform", {}).get("architecture") == arch
        ]
        assert len(
            manifests) == 1, f"Expected one Linux {arch} manifest: {index}"
        manifest = json.loads(blob(manifests[0]))
        checksums = []
        for layer in manifest["layers"]:
            stream = io.BytesIO(blob(layer))
            if layer.get("mediaType", "").endswith("+zstd"):
                stream = zstandard.ZstdDecompressor().stream_reader(stream)
            with stream, tarfile.open(fileobj=stream, mode="r|*") as contents:
                for member in contents:
                    if member.name.removeprefix(
                            "./"
                    ) == f"nginx/{NGINX_VERSION}/ngx_http_datadog_module.so":
                        checksums.append(
                            hashlib.sha256(
                                contents.extractfile(
                                    member).read()).hexdigest())
        assert len(checksums
                   ) == 1, f"Missing or duplicate Nginx {NGINX_VERSION} module"
        return checksums[0]


def build(docker, arch):
    subprocess.run(["git", "submodule", "update", "--init", "--recursive"],
                   cwd=ROOT,
                   check=True)
    settings = yaml.safe_load(
        (ROOT / ".gitlab/common.yml").read_text())["variables"]
    toolchain = settings["MUSL_TOOLCHAIN_IMAGE"].replace(
        "$CI_REGISTRY", "registry.ddbuild.io/ci/nginx-datadog")
    docker.run("pull", "--platform", f"linux/{arch}", toolchain)
    builder = docker.run("run", "-d", "--cpus", "6", "--platform",
                         f"linux/{arch}", "--label", LABEL, toolchain, "sleep",
                         "infinity")
    output = Path(tempfile.mkdtemp(prefix="package-", dir=docker.artifacts))
    try:
        with tempfile.TemporaryDirectory() as temporary:
            archive = Path(temporary) / "checkout.tar"
            tracked = subprocess.check_output(["git", "ls-files", "-z"],
                                              cwd=ROOT).decode().split("\0")
            with tarfile.open(archive, "w") as sources:
                for name in filter(None, tracked):

                    def include(member):
                        parts = PurePosixPath(member.name).parts
                        return None if any(part in (".git", ".venv",
                                                    "artifacts", "__pycache__",
                                                    ".pytest_cache")
                                           for part in parts) else member

                    sources.add(ROOT / name, arcname=name, filter=include)
            docker.run("cp", archive, f"{builder}:/tmp/checkout.tar")
        docker.run("exec", builder, "mkdir", "/checkout")
        docker.run("exec", builder, "tar", "-xf", "/tmp/checkout.tar", "-C",
                   "/checkout")
        normalized = {"amd64": "x86_64", "arm64": "aarch64"}[arch]
        docker.run("exec",
                   "-w",
                   "/checkout",
                   builder,
                   "make",
                   "build-musl-aux",
                   f"ARCH={normalized}",
                   f"NGINX_VERSION={NGINX_VERSION}",
                   "RUM=ON",
                   "WAF=OFF",
                   "MAKE_JOB_COUNT=8",
                   timeout=1200)
        sources = output / "sources"
        module_dir = sources / "nginx" / NGINX_VERSION
        module_dir.mkdir(parents=True, exist_ok=True)
        docker.run(
            "cp",
            f"{builder}:/checkout/.musl-build/ngx_http_datadog_module.so",
            module_dir)
        commit = subprocess.check_output(
            ["git", "rev-parse", "--short", "HEAD"], cwd=ROOT,
            text=True).strip()
        version = f"0.0.1-{commit}"
        (sources / "version").write_text(version + "\n")
        return assemble(docker, arch, sources, version, output)
    finally:
        docker.run("rm", "-f", "-v", builder)


def assemble(docker, arch, sources, version, output):
    docker.run("pull", "--platform", "linux/amd64", PACKAGER)
    packager = docker.run("run", "-d", "--platform", "linux/amd64", "--label",
                          LABEL, "--entrypoint", "sleep", PACKAGER, "infinity")
    try:
        docker.run("exec", packager, "mkdir", "/package")
        docker.run("cp", sources, f"{packager}:/package/sources")
        docker.run("exec", "-w", "/package", packager, "datadog-package",
                   "create", f"--version={version}",
                   "--package=datadog-apm-library-nginx", f"--arch={arch}",
                   "--os=linux", "./sources")
        docker.run("cp", f"{packager}:/package/.", output)
        packages = list(output.glob("*.tar"))
        assert len(packages) == 1, packages
        expected = hashlib.sha256(
            (sources / "nginx" / NGINX_VERSION /
             "ngx_http_datadog_module.so").read_bytes()).hexdigest()
        assert module_checksum(packages[0], arch) == expected
        return packages[0]
    finally:
        docker.run("rm", "-f", "-v", packager)
