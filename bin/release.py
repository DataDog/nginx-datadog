#!/usr/bin/env python3
"""
This script automates the release process for the nginx-dataadog module, 
ingress-nginx init container and the installer.

Usage:
======
To release nginx-datadog modules:
./release.py --ci-token <CI_TOKEN> --version-tag v1.3.1 --workflow-id <CI_RELEASE_WORKFLOW_ID> nginx-module

To release ingress-nginx docker init containers:
./release.py --ci-token <CI_TOKEN> --version-tag v1.3.1 --workflow-id <CI_RELEASE_WORKFLOW_ID> ingres-nginx

To release the installer:
./release.py --ci-token <CI_TOKEN> --version-tag v0.3.3 --workflow-id <CI_RELEASE_WORKFLOW_ID> installer
"""

import re
import argparse
import itertools
import json
import os
import shlex
import shutil
import subprocess
import sys
import tempfile
import urllib.request
import tarfile
import typing

from pathlib import Path
from collections import defaultdict
from ingress_nginx import build_init_container, create_multiarch_images


class VerboseDict(dict):
    """This makes debugging a little easier."""

    def __getitem__(self, key):
        try:
            return super().__getitem__(key)
        except KeyError as error:
            # In addition to the missing key, print the object itself.
            message = f"{repr(key)} is not in {self}"
            error.args = (message,)
            raise error


class MissingDependency(Exception):
    pass


def get_gh():
    exe_path = shutil.which("gh")
    if exe_path is None:
        raise MissingDependency(
            'The "gh" command must be available to publish a release to GitHub.  Installation instructions are available at <https://cli.github.com/>.'
        )
    return exe_path


def get_gpg():
    exe_path = shutil.which("gpg")
    if exe_path is None:
        raise MissingDependency(
            'The "gpg" command must be available and configured to be able to create detached signatures of release artifacts.'
        )
    return exe_path


def get_git():
    exe_path = shutil.which("git")
    if exe_path is None:
        raise MissingDependency(
            'The "git" command must be available to tag the release commit.'
        )
    return exe_path


def run(command, *args, **kwargs):
    command = [str(arg) for arg in command]
    print("+", shlex.join(command), file=sys.stderr)
    return subprocess.run(command, *args, **kwargs)


def send_ci_request(path, payload=None, method=None):
    headers = {"Circle-Token": ci_api_token}
    if payload is not None:
        headers["Content-Type"] = "application/json; charset=utf-8"
        payload = json.dumps(payload).encode("utf8")

    API_URL = "https://circleci.com/api/v2"
    url = f"{API_URL}{path}"
    request = urllib.request.Request(url, data=payload, headers=headers, method=method)
    print("+", request.get_method(), request.full_url, request.data or "")

    try:
        response = urllib.request.urlopen(request)
        status = response.status
    except urllib.error.HTTPError as error:
        response = error
        status = error.code

    try:
        response_body = json.load(response, object_hook=VerboseDict)
    except Exception as error:
        print(f"Unable to parse response body from response with status {status}.")
        raise

    if status < 200 or status > 299:
        raise Exception(f"HTTP error response {status}: {response_body}")

    return status, response_body


def send_ci_request_paged(path, payload=None, method=None):
    items = []
    query = ""
    while True:
        _, response = send_ci_request(f"{path}{query}", payload=payload, method=method)
        items += response["items"]

        next_page = response.get("next_page_token")
        if next_page is None:
            break
        query = f"?page-token={next_page}"

    return items


def get_workflow_jobs(workflow_id: str):
    jobs = send_ci_request_paged(f"/workflow/{workflow_id}/job")

    # Make sure all jobs run successfully
    for job in jobs:
        if job["status"] != "success":
            print("Found unsuccessful jobs")
            return None

    return jobs


def download_file(url, destination):
    print(f"Downloading {url} to {destination}")
    response = urllib.request.urlopen(url)
    with open(destination, "wb") as output:
        shutil.copyfileobj(response, output)


def package(files, out):
    if not isinstance(files, list):
        files = [files]

    with tarfile.open(out, "w:gz") as tar:
        for f in files:
            if not f.is_file():
                print(f"{f} is not a valid file and will be skipped")
                continue

            tar.add(f, arcname=os.path.basename(f))


def sign_package(package_path: str) -> None:
    command = [gpg_exe, "--armor", "--detach-sign", package_path]
    run(command, check=True)


def prepare_installer_release_artifact(work_dir, build_job_number, arch):
    artifacts = send_ci_request_paged(
        f"/project/gh/DataDog/nginx-datadog/{build_job_number}/artifacts"
    )
    module_url = None
    for artifact in artifacts:
        name = artifact["path"]
        if name == "nginx-configurator":
            module_url = artifact["url"]

    if module_url is None:
        raise Exception(
            f"Job number {build_job_number} doesn't have an 'nginx-configurator' build artifact."
        )

    module_path = work_dir / "nginx-configurator"
    download_file(module_url, module_path)

    # Package and sign
    tarball_path = work_dir / f"nginx-configurator-{arch}.tgz"
    package(module_path, out=tarball_path)
    sign_package(tarball_path)


def prepare_release_artifact(work_dir, build_job_number, version, arch, waf):
    waf_suffix = "-appsec" if waf else ""
    artifacts = send_ci_request_paged(
        f"/project/gh/DataDog/nginx-datadog/{build_job_number}/artifacts"
    )
    module_url = None
    module_url_dbg = None
    for artifact in artifacts:
        name = artifact["path"]
        if name == "ngx_http_datadog_module.so":
            module_url = artifact["url"]
        elif name == "ngx_http_datadog_module.so.debug":
            module_url_dbg = artifact["url"]

    if module_url is None:
        raise Exception(
            f"Job number {build_job_number} doesn't have an 'ngx_http_datadog_module.so' build artifact."
        )
    if module_url_dbg is None:
        raise Exception(
            f"Job number {build_job_number} doesn't have an 'ngx_http_datadog_module.so.debug' build artifact."
        )

    module_path = work_dir / "ngx_http_datadog_module.so"
    download_file(module_url, module_path)

    module_debug_path = work_dir / "ngx_http_datadog_module.so.debug"
    download_file(module_url_dbg, module_debug_path)

    # Package and sign .so
    tarball_path = (
        work_dir / f"ngx_http_datadog_module{waf_suffix}-{arch}-{version}.so.tgz"
    )
    package(module_path, out=tarball_path)
    sign_package(tarball_path)

    # Package and sign .so.debug
    debug_tarball_path = (
        work_dir / f"ngx_http_datadog_module{waf_suffix}-{arch}-{version}.so.debug.tgz"
    )
    package(module_debug_path, out=debug_tarball_path)
    sign_package(debug_tarball_path)


def release_installer(args) -> int:
    """
    This subcommand function downloads installer artifacts from the release workflow
    and publishes them to a GitHub release.

    Requirements:
        - You must be logged into the GitHub CLI (gh) to authenticate and publish the release.
    """
    jobs = get_workflow_jobs(args.workflow_id)
    if not jobs:
        return 1

    with tempfile.TemporaryDirectory() as work_dir:
        for job in jobs:
            if job["name"].startswith("build installer "):
                # name should be something like "build installer on arm64"
                match = re.match(r"build installer on (amd64|arm64)", job["name"])
                if match is None:
                    raise Exception(f'Job name does not match regex "{re}": {job}')
                arch = match.groups()[0]
                prepare_installer_release_artifact(work_dir, job["job_number"], arch)

        pubkey_file = os.path.join(work_dir, "pubkey.gpg")
        run([gpg_exe, "--output", pubkey_file, "--armor", "--export"], check=True)

        # We've tgz'd and signed all of our release modules.
        # Now let's send them to GitHub in a release via `gh release create`.
        release_files = itertools.chain(
            Path(work_dir).glob("*.tgz"),
            Path(work_dir).glob("*.tgz.asc"),
            (pubkey_file,),
        )

        command = [
            gh_exe,
            "release",
            "create",
            "-R",
            "DataDog/nginx-datadog",
            "--prerelease",
            "--draft",
            "--title",
            options.version_tag,
            "--notes",
            "TODO",
            options.version_tag,
            *release_files,
        ]
        run(command, check=True)

    return 0


def release_ingress_nginx(args: typing.Any) -> int:
    """
    This subcommand function retrieves the modules from the CI workflow, builds the ingress-nginx init container,
    and publishes it to a Docker registry.

    Requirements:
        - You must be logged into the Docker registry to push the built container image.
    """
    jobs = get_workflow_jobs(args.workflow_id)
    if not jobs:
        return 1

    collected_images = defaultdict(list)

    for job in jobs:
        if job["name"].startswith("build ingress-nginx"):
            match = re.match(
                r"build ingress-nginx-([\d.]+) on (amd64|arm64)", job["name"]
            )
            if match is None:
                raise Exception(f'Job name does not match regex "{re}": {job}')
            version, arch = match.groups()

            image_version = f"v{version}-dd.{args.version_tag}"

            artifacts = send_ci_request_paged(
                f"/project/gh/DataDog/nginx-datadog/{job['job_number']}/artifacts"
            )
            module_url = None
            for artifact in artifacts:
                name = artifact["path"]
                if name == "ngx_http_datadog_module.so":
                    module_url = artifact["url"]

            if module_url is None:
                raise Exception(
                    f"Job number {job['job_number']} doesn't have an \"ngx_http_datadog_module.so\" build artifact."
                )

            with tempfile.TemporaryDirectory() as work_dir:
                module_path = Path(work_dir) / "ngx_http_datadog_module.so"
                download_file(module_url, module_path)

                args.push = True
                args.platform = f"linux/{arch}"
                args.image_name = f"{args.registry}:{image_version}-{arch}"
                args.module_path = work_dir
                build_init_container(args)

                image_no_arch = f"{args.registry}:{image_version}"

                collected_images[image_no_arch].append(args.image_name)
                collected_images[f"{args.registry}:v{version}"].append(args.image_name)

    create_multiarch_images(collected_images)

    return 0


def release_module(args) -> int:
    """
    This subcommand function downloads NGINX module artifacts from the release workflow
    and publishes them to a GitHub release.

    Requirements:
        - You must be logged into the GitHub CLI (gh) to authenticate and interact with the GitHub API.
    """
    jobs = get_workflow_jobs(args.workflow_id)
    if not jobs:
        return 1

    with tempfile.TemporaryDirectory() as work_dir:
        work_dir = Path(work_dir)
        print("Working directory is", work_dir)

        for job in jobs:
            # See the response schema for a list of statuses:
            # https://circleci.com/docs/api/v2/index.html#operation/listWorkflowJobs
            if (
                job["name"].startswith("build ")
                and not job["name"].startswith("build installer ")
                and not job["name"].startswith("build ingress-nginx")
            ):
                # name should be something like "build 1.25.4 on arm64 WAF ON"
                match = re.match(
                    r"build ([\d.]+) on (amd64|arm64) WAF (ON|OFF)", job["name"]
                )
                if match is None:
                    raise Exception(f'Job name does not match regex "{re}": {job}')
                version, arch, waf = match.groups()
                prepare_release_artifact(
                    work_dir, job["job_number"], version, arch, waf == "ON"
                )

        pubkey_file = os.path.join(work_dir, "pubkey.gpg")
        run([gpg_exe, "--output", pubkey_file, "--armor", "--export"], check=True)

        # We've tgz'd and signed all of our release modules.
        # Now let's send them to GitHub in a release via `gh release create`.
        release_files = itertools.chain(
            Path(work_dir).glob("*.tgz"),
            Path(work_dir).glob("*.tgz.asc"),
            (pubkey_file,),
        )

        command = [
            gh_exe,
            "release",
            "create",
            "-R",
            "DataDog/nginx-datadog",
            "--prerelease",
            "--draft",
            "--title",
            options.version_tag,
            "--notes",
            "TODO",
            options.version_tag,
            *release_files,
        ]
        run(command, check=True)

    return 0


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Build and publish a release of nginx-datadog.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument(
        "--version-tag",
    )
    parser.add_argument("--ci-token", help="Circle CI Token", required=True)
    parser.add_argument(
        "workflow_id",
        type=str,
        help="ID of the release workflow. Find in job url. Example: https://app.circleci.com/pipelines/github/DataDog/nginx-datadog/542/workflows/<WORKFLOW_ID>",
    )

    subparsers = parser.add_subparsers()
    subparsers.required = True

    # nginx-module
    nginx_module_parser = subparsers.add_parser(
        "nginx-module", help="Release the NGINX module"
    )
    nginx_module_parser.set_defaults(func=release_module)

    # installer subcommand
    installer_parser = subparsers.add_parser(
        "installer", help="Release the NGINX installer"
    )
    installer_parser.set_defaults(func=release_installer)
    installer_parser.add_argument(
        "--installer",
        help="Release the NGINX installer",
        action=argparse.BooleanOptionalAction,
    )

    # ingress-nginx subcommand
    ingress_nginx_parser = subparsers.add_parser(
        "ingress-nginx", help="Release ingress-nginx init containers"
    )
    ingress_nginx_parser.set_defaults(func=release_ingress_nginx)
    ingress_nginx_parser.add_argument(
        "--registry",
        help="Docker registry where images will be pushed",
        default="datadog/ingress-nginx-injection",
    )
    options = parser.parse_args()

    ci_api_token = options.ci_token
    try:
        gh_exe = get_gh()
        gpg_exe = get_gpg()
        git_exe = get_git()
    except (MissingDependency, ValueError) as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)

    print(
        {
            "token": ci_api_token,
            "gh": gh_exe,
            "gpg": gpg_exe,
            "git": git_exe,
        }
    )

    sys.exit(options.func(options))
