#!/usr/bin/env python3
"""
This script automates the release process for the nginx-datadog module and
ingress-nginx init container.

It fetches built artifacts from a GitLab CI pipeline (triggered by a version
tag) and publishes them.

Usage:
======
To release nginx-datadog modules:
./release.py --version-tag v1.3.1 --pipeline-id <PIPELINE_ID> nginx-module

To release ingress-nginx docker init containers:
./release.py --version-tag v1.3.1 --pipeline-id <PIPELINE_ID> ingress-nginx

Authentication:
===============
The script resolves a GitLab API token in this order:
  1. --ci-token command line argument
  2. CI_JOB_TOKEN environment variable (in GitLab CI)
  3. GITLAB_TOKEN environment variable
  4. Token from glab CLI config (for local development)
"""

import re
import argparse
import http.client
import io
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
import zipfile

from pathlib import Path
from collections import defaultdict
from typing import Any, Final
from urllib.parse import quote
from ingress_nginx import build_init_container, create_multiarch_images

GITLAB_HOST: Final[str] = "https://gitlab.ddbuild.io"
GITLAB_API: Final[str] = f"{GITLAB_HOST}/api/v4"
GITLAB_PROJECT: Final[str] = "DataDog/nginx-datadog"
GITLAB_PROJECT_ENCODED: Final[str] = quote(GITLAB_PROJECT, safe="")

MODULE_NAME: Final[str] = "ngx_http_datadog_module.so"
MODULE_DEBUG_NAME: Final[str] = MODULE_NAME + ".debug"


class MissingDependency(Exception):
    pass


def get_gh():
    exe_path = shutil.which("gh")
    if exe_path is None:
        raise MissingDependency(
            'The "gh" command must be available to publish a release to GitHub. Installation instructions are available at <https://cli.github.com/>.'
        )
    return exe_path


def get_gpg():
    exe_path = shutil.which("gpg")
    if exe_path is None:
        raise MissingDependency(
            'The "gpg" command must be available and configured to be able to create detached signatures of release artifacts.'
        )
    return exe_path


def resolve_gitlab_token(cli_token: str | None = None) -> str:
    """Resolve a GitLab API token from multiple sources."""
    if cli_token:
        return cli_token

    # GitLab CI job token
    if token := os.environ.get("CI_JOB_TOKEN"):
        return token

    if token := os.environ.get("GITLAB_TOKEN"):
        return token

    # Try extracting from glab CLI config
    if glab_exe := shutil.which("glab"):
        try:
            result = subprocess.run(
                [
                    glab_exe, "config", "get", "token", "--host",
                    "gitlab.ddbuild.io"
                ],
                capture_output=True,
                text=True,
            )
            if result.returncode == 0 and result.stdout.strip():
                return result.stdout.strip()
        except (subprocess.SubprocessError, OSError) as exc:
            print(
                f"WARNING: glab found at {glab_exe} but token extraction failed: {exc}",
                file=sys.stderr)

    raise MissingDependency(
        "No GitLab API token found. Provide one via --ci-token, "
        "CI_JOB_TOKEN, GITLAB_TOKEN, or by logging in with "
        "'glab auth login --hostname gitlab.ddbuild.io'.")


def run(command, *args, **kwargs):
    command = [str(arg) for arg in command]
    print("+", shlex.join(command), file=sys.stderr)
    return subprocess.run(command, *args, **kwargs)


def gitlab_api_request(path: str, token: str) -> http.client.HTTPResponse:
    """Make an authenticated GET request to the GitLab API."""
    if os.environ.get("CI_JOB_TOKEN") and token == os.environ.get(
            "CI_JOB_TOKEN"):
        headers = {"JOB-TOKEN": token}
    else:
        headers = {"PRIVATE-TOKEN": token}

    url = f"{GITLAB_API}{path}"
    request = urllib.request.Request(url, headers=headers)
    print("+", request.get_method(), url, file=sys.stderr)

    try:
        response = urllib.request.urlopen(request)
    except urllib.error.HTTPError as error:
        body = error.read().decode("utf-8", errors="replace")
        raise Exception(
            f"GitLab API error {error.code} for {url}: {body}") from error

    return response


def gitlab_api_json(path: str, token: str) -> Any:
    """Make a GitLab API request and return parsed JSON."""
    response = gitlab_api_request(path, token)
    return json.load(response)


def get_pipeline_jobs(pipeline_id: str,
                      token: str) -> list[dict[str, Any]] | None:
    """Retrieve all jobs for a GitLab pipeline, handling pagination."""
    jobs: list[dict[str, Any]] = []
    page = 1
    while True:
        page_jobs = gitlab_api_json(
            f"/projects/{GITLAB_PROJECT_ENCODED}/pipelines/{pipeline_id}/jobs?per_page=100&page={page}",
            token,
        )
        if not page_jobs:
            break
        jobs.extend(page_jobs)
        page += 1

    # Verify all jobs succeeded (skip jobs that haven't been triggered or aren't relevant)
    for job in jobs:
        if job["status"] in ("success", "manual", "skipped", "created"):
            continue
        print(
            f"WARNING: Job '{job['name']}' (id={job['id']}) has status '{job['status']}'"
        )
        return None

    return jobs


def download_job_artifacts_zip(job_id: int, token: str) -> zipfile.ZipFile:
    """Download the full artifacts archive for a job and return as ZipFile."""
    path = f"/projects/{GITLAB_PROJECT_ENCODED}/jobs/{job_id}/artifacts"
    response = gitlab_api_request(path, token)
    data = response.read()
    return zipfile.ZipFile(io.BytesIO(data))


def extract_artifact_from_zip(zip_file: zipfile.ZipFile, artifact_path: str,
                              destination: Path) -> None:
    """Extract a specific file from a job artifacts zip."""
    with zip_file.open(artifact_path) as src, open(destination, "wb") as dst:
        shutil.copyfileobj(src, dst)
    print(f"Extracted {artifact_path} to {destination}")


def package(files, out):
    if not isinstance(files, list):
        files = [files]

    with tarfile.open(out, "w:gz") as tar:
        for f in files:
            if not f.is_file():
                raise FileNotFoundError(
                    f"Cannot package '{f}': file does not exist. "
                    f"This indicates a problem with artifact extraction.")
            tar.add(f, arcname=os.path.basename(f))


def sign_package(package_path: str | Path) -> None:
    command = [gpg_exe, "--armor", "--detach-sign", package_path]
    run(command, check=True)


def prepare_release_artifact(work_dir: Path, job_id: int,
                             artifact_base_path: str, flavor: str | None,
                             version: str, arch: str, waf: bool,
                             token: str) -> None:
    flavor_prefix = f"{flavor}-" if flavor else ""
    waf_suffix = "-appsec" if waf else ""

    zip_file = download_job_artifacts_zip(job_id, token)

    module_path = work_dir / MODULE_NAME
    module_debug_path = work_dir / MODULE_DEBUG_NAME

    so_artifact = f"{artifact_base_path}/{MODULE_NAME}"
    dbg_artifact = f"{artifact_base_path}/{MODULE_DEBUG_NAME}"

    zip_contents = zip_file.namelist()

    for artifact in (so_artifact, dbg_artifact):
        if artifact not in zip_contents:
            raise Exception(
                f"Job {job_id} artifacts don't contain '{artifact}'. "
                f"Available: {zip_contents}")

    extract_artifact_from_zip(zip_file, so_artifact, module_path)
    extract_artifact_from_zip(zip_file, dbg_artifact, module_debug_path)

    # Package and sign .so
    tarball_path = (
        work_dir /
        f"{flavor_prefix}ngx_http_datadog_module{waf_suffix}-{arch}-{version}.so.tgz"
    )
    package(module_path, out=tarball_path)
    sign_package(tarball_path)

    # Package and sign .so.debug
    debug_tarball_path = (
        work_dir /
        f"{flavor_prefix}ngx_http_datadog_module{waf_suffix}-{arch}-{version}.so.debug.tgz"
    )
    package(module_debug_path, out=debug_tarball_path)
    sign_package(debug_tarball_path)


# GitLab matrix job name patterns:
#   build-nginx-all: [amd64, 1.28.1, ON]
#   build-openresty-all: [amd64, 1.27.1.2, ON]
#   build-ingress-nginx-all: [amd64, 1.14.3]

NGINX_BUILD_RE: re.Pattern[str] = re.compile(
    r"build-nginx-all: \[(amd64|arm64), ([\d.]+), (ON|OFF)\]")
OPENRESTY_BUILD_RE: re.Pattern[str] = re.compile(
    r"build-openresty-all: \[(amd64|arm64), ([\d.]+), (ON|OFF)\]")
INGRESS_BUILD_RE: re.Pattern[str] = re.compile(
    r"build-ingress-nginx-all: \[(amd64|arm64), ([\d.]+)\]")


def release_ingress_nginx(args: typing.Any) -> int:
    """
    This subcommand function retrieves the modules from the CI pipeline, builds the ingress-nginx init container,
    and publishes it to a Docker registry.

    Requirements:
        - You must be logged into the Docker registry to push the built container image.
    """
    jobs = get_pipeline_jobs(args.pipeline_id, gitlab_token)
    if jobs is None:
        print("ERROR: Pipeline contains unsuccessful jobs. Aborting release.",
              file=sys.stderr)
        return 1
    if not jobs:
        print(f"ERROR: No jobs found for pipeline {args.pipeline_id}.",
              file=sys.stderr)
        return 1

    collected_images = defaultdict(list)

    for job in jobs:
        match = INGRESS_BUILD_RE.match(job["name"])
        if not match:
            continue

        arch, version = match.groups()
        version = f"v{version}"
        image_version = f"{version}-dd.{args.version_tag}"

        # Download the module from the job artifacts
        artifact_path = f"artifacts-ingress/{arch}/{version.lstrip('v')}"

        with tempfile.TemporaryDirectory() as work_dir_str:
            work_dir = Path(work_dir_str)
            zip_file = download_job_artifacts_zip(job["id"], gitlab_token)

            so_artifact = f"{artifact_path}/{MODULE_NAME}"
            if so_artifact not in zip_file.namelist():
                raise Exception(
                    f"Job {job['id']} ({job['name']}) doesn't have "
                    f"'{so_artifact}' in artifacts.")

            module_path = work_dir / MODULE_NAME
            extract_artifact_from_zip(zip_file, so_artifact, module_path)

            args.push = True
            args.platform = f"linux/{arch}"
            args.image_name = f"{args.registry}:{image_version}-{arch}"
            args.module_path = str(work_dir)
            build_init_container(args)

            image_no_arch = f"{args.registry}:{image_version}"

            collected_images[image_no_arch].append(args.image_name)
            collected_images[f"{args.registry}:{version}"].append(
                args.image_name)

    if not collected_images:
        print(
            f"ERROR: No ingress-nginx build jobs matched in pipeline {args.pipeline_id}. "
            f"Job names: {[j['name'] for j in jobs]}",
            file=sys.stderr,
        )
        return 1

    create_multiarch_images(collected_images)

    return 0


def release_module(args) -> int:
    """
    This subcommand function downloads NGINX module artifacts from the release pipeline
    and publishes them to a GitHub release.

    Requirements:
        - You must be logged into the GitHub CLI (gh) to authenticate and interact with the GitHub API.
    """
    jobs = get_pipeline_jobs(args.pipeline_id, gitlab_token)
    if jobs is None:
        print("ERROR: Pipeline contains unsuccessful jobs. Aborting release.",
              file=sys.stderr)
        return 1
    if not jobs:
        print(f"ERROR: No jobs found for pipeline {args.pipeline_id}.",
              file=sys.stderr)
        return 1

    with tempfile.TemporaryDirectory() as work_dir_str:
        work_dir = Path(work_dir_str)
        print("Working directory is", work_dir)

        for job in jobs:
            # Match nginx builds
            match = NGINX_BUILD_RE.match(job["name"])
            if match:
                arch, nginx_version, waf = match.groups()
                artifact_base = f"artifacts/{arch}/{nginx_version}/{waf}"
                prepare_release_artifact(work_dir, job["id"], artifact_base,
                                         None, nginx_version, arch,
                                         waf == "ON", gitlab_token)
                continue

            # Match openresty builds
            match = OPENRESTY_BUILD_RE.match(job["name"])
            if match:
                arch, resty_version, waf = match.groups()
                artifact_base = f"artifacts-openresty/{arch}/{resty_version}/{waf}"
                prepare_release_artifact(work_dir, job["id"], artifact_base,
                                         "openresty", resty_version, arch,
                                         waf == "ON", gitlab_token)
                continue

        release_tarballs = list(Path(work_dir).glob("*.tgz"))
        if not release_tarballs:
            print(
                f"ERROR: No build artifacts matched in pipeline {args.pipeline_id}. "
                f"Job names: {[j['name'] for j in jobs]}",
                file=sys.stderr,
            )
            return 1

        pubkey_file = os.path.join(work_dir, "pubkey.gpg")
        run([gpg_exe, "--output", pubkey_file, "--armor", "--export"],
            check=True)

        release_files = itertools.chain(
            release_tarballs,
            Path(work_dir).glob("*.tgz.asc"),
            (pubkey_file, ),
        )

        command = [
            gh_exe,
            "release",
            "create",
            args.version_tag,
            "--prerelease",
            "--draft",
            "--generate-notes",
            "--verify-tag",
            "--repo",
            "DataDog/nginx-datadog",
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
        required=True,
        help="Tag of the version to release (example: 'v1.2.3')")
    parser.add_argument("--ci-token",
                        help="GitLab API token (auto-detected if not set)")
    parser.add_argument(
        "--pipeline-id",
        type=str,
        required=True,
        help="ID of the GitLab CI pipeline. Find it in the pipeline URL: "
        "https://gitlab.ddbuild.io/DataDog/nginx-datadog/-/pipelines/<PIPELINE_ID>",
    )

    subparsers = parser.add_subparsers()
    subparsers.required = True

    # nginx-module
    nginx_module_parser = subparsers.add_parser(
        "nginx-module", help="Release the NGINX module")
    nginx_module_parser.set_defaults(func=release_module)

    # ingress-nginx subcommand
    ingress_nginx_parser = subparsers.add_parser(
        "ingress-nginx", help="Release ingress-nginx init containers")
    ingress_nginx_parser.set_defaults(func=release_ingress_nginx)
    ingress_nginx_parser.add_argument(
        "--registry",
        help="Docker registry where images will be pushed",
        default="datadog/ingress-nginx-injection",
    )
    options = parser.parse_args()

    try:
        gitlab_token = resolve_gitlab_token(options.ci_token)
        gh_exe = get_gh()
        gpg_exe = get_gpg()
    except (MissingDependency, ValueError) as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)

    print({
        "gitlab_host": GITLAB_HOST,
        "gh": gh_exe,
        "gpg": gpg_exe,
    })

    sys.exit(options.func(options))
