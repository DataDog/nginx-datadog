#!/usr/bin/env python3
"""
This script manages the process of building a version of the module compatible with ingress-nginx.

Usage:
======
1. Prepare the environment by downloading the specified ingress-nginx version source code.
This command fetches ingress-nginx version 1.10.4 and outputs it to the 'controller-src' directory.

./ingress-nginx prepare --ingress-nginx-version v1.10.4 --output_dir controller-src

2. Set up the build system using CMake.
cmake -B build -DNGINX_SRC_DIR=controller-src .

3: Build the module:
cmake --build build -j
"""
import os
import sys
import argparse
import tempfile
import subprocess
import urllib.request
import tarfile
import shutil
import typing

CWD = os.getcwd()
DRY_RUN = False
PROJECT_DIR = os.path.dirname(os.path.dirname(os.path.realpath(__file__)))


def get_underlying_nginx_version(controller_version: str) -> str:
    # Map an ingress-nginx version to an NGINX version
    mapping = {
        "v1.11.3": "1.25.5",
        "v1.11.2": "1.25.5",
        "v1.11.1": "1.25.5",
        "v1.11.0": "1.25.5",
        "v1.10.4": "1.25.5",
        "v1.10.3": "1.25.5",
        "v1.10.2": "1.25.5",
        "v1.10.1": "1.25.3",
        "v1.10.0": "1.25.3",
        "v1.9.6": "1.21.6",
    }
    return mapping.get(controller_version, "")


def run_cmd(cmd: str) -> None:
    print(cmd)
    if not DRY_RUN:
        subprocess.run(cmd, shell=True)  # , check=True)


def clone_nginx(version: str, out_dir: str) -> str:
    url = f"https://nginx.org/download/nginx-{version}.tar.gz"

    with tempfile.TemporaryDirectory() as work_dir:
        tarball_abspath = os.path.join(work_dir, "nginx.tar.gz")
        urllib.request.urlretrieve(url, tarball_abspath)

        with tarfile.open(tarball_abspath) as tar:
            tar.extractall(path=work_dir)

        nginx_src_dir = os.path.join(work_dir, f"nginx-{version}")
        shutil.move(nginx_src_dir, out_dir)

    return out_dir


def get_patch_directory(version: str, ingress_rootdir: str) -> str:
    mapping = {
        "v1.11.3": f"{ingress_rootdir}/images/nginx/rootfs/patches",
        "v1.11.2": f"{ingress_rootdir}/images/nginx-1.25/rootfs/patches",
        "v1.11.1": f"{ingress_rootdir}/images/nginx-1.25/rootfs/patches",
        "v1.11.0": f"{ingress_rootdir}/images/nginx-1.25/rootfs/patches",
        "v1.10.4": f"{ingress_rootdir}/images/nginx-1.25/rootfs/patches",
        "v1.10.3": f"{ingress_rootdir}/images/nginx-1.25/rootfs/patches",
        "v1.10.2": f"{ingress_rootdir}/images/nginx-1.25/rootfs/patches",
        "v1.10.1": f"{ingress_rootdir}/images/nginx-1.25/rootfs/patches",
        "v1.10.0": f"{ingress_rootdir}/images/nginx-1.25/rootfs/patches",
        "v1.10.0": f"{ingress_rootdir}/images/nginx-1.25/rootfs/patches",
        "v1.9.6": f"{ingress_rootdir}/images/nginx/rootfs/patches",
    }
    return mapping.get(version, "")


def patch_nginx(src_dir: str, patch_dir: str) -> None:
    if DRY_RUN:
        return
    for patch_file in os.listdir(patch_dir):
        patch_file_abspath = os.path.join(patch_dir, patch_file)
        if patch_file.endswith(".txt"):
            run_cmd(f"patch -p0 -d {src_dir} -f < {patch_file_abspath}")
        else:
            run_cmd(f"patch -p1 -d {src_dir} -f < {patch_file_abspath}")


def prepare(args) -> int:
    """
    Automates the process of preparing an NGINX source directory that is compatible
    with the ingress-nginx controller.

    What the script does:
    =====================
    1. Clones the ingress-nginx repository based on a specified controller version.
    2. Downloads the NGINX source code corresponding to the version used by ingress-nginx.
    3. Applies any necessary patches from ingress-nginx to the NGINX source code.
    """
    global DRY_RUN
    DRY_RUN = args.dry_run

    if os.path.exists(args.output_dir):
        print(
            f"Warning: The directory '{args.output_dir}' already exists and its contents will be overwritten."
        )
        shutil.rmtree(args.output_dir, ignore_errors=True)

    nginx_version = get_underlying_nginx_version(args.ingress_nginx_version)
    if not nginx_version:
        print(
            f"Error: The specified version '{args.ingress_nginx_version}' is not recognized as a valid ingress-nginx version. Please check the version and try again."
        )
        return 1

    with tempfile.TemporaryDirectory() as work_dir:
        ingress_nginx_path = os.path.join(work_dir, "ingress-nginx")
        run_cmd(
            f"git clone --branch controller-{args.ingress_nginx_version} https://github.com/kubernetes/ingress-nginx.git {ingress_nginx_path}"
        )

        nginx_src_dir = clone_nginx(nginx_version, args.output_dir)

        patches_path = get_patch_directory(args.ingress_nginx_version,
                                           ingress_nginx_path)
        if patches_path and os.path.exists(patches_path):
            patch_nginx(nginx_src_dir, patches_path)

    return 0


def build_init_container(args) -> int:
    dockerfile_path = os.path.join(PROJECT_DIR, "injection", "ingress-nginx")
    run_cmd(
        f"docker build --progress=plain --no-cache --build-context build={args.module_path} --platform {args.platform} --tag {args.image_name} {dockerfile_path}"
    )

    if args.push:
        run_cmd(f"docker push {args.image_name}")

    return 0


def create_multiarch_images(tag_map: dict[str, typing.Any]) -> None:
    for image, tags in tag_map.items():
        run_cmd(f"docker buildx imagetools create -t {image} {' '.join(tags)}")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Script managing ingress-nginx")
    subparsers = parser.add_subparsers()

    # prepare subcommand
    prepare_parser = subparsers.add_parser(
        "prepare",
        help=
        "Prepare an NGINX source directory to be compatible with a specified ingress-nginx version",
    )
    prepare_parser.set_defaults(func=prepare)
    prepare_parser.add_argument(
        "--ingress-nginx-version",
        metavar="INGRESS_NGINX_VERSION",
        help=
        "Specify the ingress-nginx version for compatibility (e.g., v1.10.3).",
        required=True,
    )
    prepare_parser.add_argument(
        "--output-dir",
        help=
        f"Directory where the prepared NGINX source code will be saved. Defaults to the current working directory ({os.path.join(CWD, 'ingress-nginx')}).",
        default=os.path.join(CWD, "ingress-nginx"),
    )
    prepare_parser.add_argument(
        "--dry-run",
        action="store_true",
        help=
        "Simulate the process without making any changes. Useful for testing what will happen.",
    )

    # build-init-container subcommand
    build_init_container_parser = subparsers.add_parser(
        "build-init-container", help="Build init-container for ingress-nginx")
    build_init_container_parser.set_defaults(func=build_init_container)
    build_init_container_parser.add_argument(
        "--image_name",
        help="Init-container docker tag name",
        default="ingress-nginx")
    build_init_container_parser.add_argument(
        "--module-path",
        help="Location of the module to package and publish",
        required=True,
    )
    build_init_container_parser.add_argument(
        "--push",
        help="Push the generated init-container to the registry",
        action="store_true",
    )

    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
