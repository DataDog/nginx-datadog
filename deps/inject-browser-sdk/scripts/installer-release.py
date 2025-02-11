#!/usr/bin/env python3
# Unless explicitly stated otherwise all files in this repository are licensed
# under the Apache 2.0 License. This product includes software developed at
# Datadog (https://www.datadoghq.com/).
#
# Copyright 2024-Present Datadog, Inc.

"""
This script automates the release process for the installer.

Usage:
======
To release the installer:
./release.py --ci-token <CI_TOKEN> --gitlab-token <GITLAB_TOKEN> --version-tag installer-0.1.3 <CI_RELEASE_WORKFLOW_ID>
"""

import re
import argparse
import itertools
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
import urllib.request
import tarfile

project_id = "DataDog%2Finject-browser-sdk" # DataDog/inject-browser-sdk

class VerboseDict(dict):
    """This makes debugging a little easier."""

    def __getitem__(self, key):
        try:
            return super().__getitem__(key)
        except KeyError as error:
            # In addition to the missing key, print the object itself.
            message = f"{repr(key)} is not in {self}"
            error.args = (message, )
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
    request = urllib.request.Request(url,
                                     data=payload,
                                     headers=headers,
                                     method=method)
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
        print(
            f"Unable to parse response body from response with status {status}."
        )
        raise

    if status < 200 or status > 299:
        raise Exception(f"HTTP error response {status}: {response_body}")

    return status, response_body


def send_ci_request_paged(path, payload=None, method=None):
    items = []
    query = ""
    while True:
        _, response = send_ci_request(f"{path}{query}",
                                      payload=payload,
                                      method=method)
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

    headers = {"Circle-Token": ci_api_token}
    request = urllib.request.Request(url,
                                     headers=headers)
    response = urllib.request.urlopen(request)

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


def upload_to_package_registry(package_path: str, package_name: str, version: str):
    """Upload a file to the GitLab Generic Package Registry."""
    API_URL = "https://gitlab.ddbuild.io/api/v4"
    file_name = os.path.basename(package_path)
    
    with open(package_path, 'rb') as f:
        file_content = f.read()
    
    url = f"{API_URL}/projects/{project_id}/packages/generic/{package_name}/{version}/{file_name}"
    headers = {
        "PRIVATE-TOKEN": gitlab_token
    }
    
    request = urllib.request.Request(
        url,
        data=file_content,
        headers=headers,
        method="PUT"
    )
    
    response = urllib.request.urlopen(request)
    if response.status != 201:
        raise Exception(f"Failed to upload package file {file_name}: {response.read()}")
    
    return url


def create_gitlab_release(version_tag: str, package_urls: set):
    """Create a GitLab release with links to the package files."""
    API_URL = "https://gitlab.ddbuild.io/api/v4"
    headers = {
        "PRIVATE-TOKEN": gitlab_token,
        "Content-Type": "application/json"
    }

    release_url = f"{API_URL}/projects/{project_id}/releases"
    assets = {
        "links": [
            {
                "name": os.path.basename(url),
                "url": url,
                "link_type": "other"
            }
            for url in package_urls
        ]
    }
    
    release_data = {
        "tag_name": version_tag,
        "name": version_tag,
        "ref": "main",
        "description": "Release created by automated script",
        "assets": assets
    }
    
    request = urllib.request.Request(
        release_url,
        data=json.dumps(release_data).encode('utf-8'),
        headers=headers,
        method="POST"
    )
    
    response = urllib.request.urlopen(request)
    if response.status != 201:
        raise Exception(f"Failed to create GitLab release: {response.read()}")


def prepare_installer_release_artifact(work_dir, build_job_number, arch):
    artifacts = send_ci_request_paged(
        f"/project/gh/DataDog/inject-browser-sdk/{build_job_number}/artifacts")
    module_url = None
    script_url = None
    for artifact in artifacts:
        name = artifact["path"]
        if name == "proxy-configurator":
            module_url = artifact["url"]
        elif name == "install-proxy-datadog.sh":
            script_url = artifact["url"]

    if module_url is None:
        raise Exception(
            f"Job number {build_job_number} doesn't have a 'proxy-configurator' build artifact."
        )

    if script_url is None:
        raise Exception(
            f"Job number {build_job_number} doesn't have an 'install-proxy-datadog.sh' build artifact."
        )

    module_path = work_dir / "proxy-configurator"
    download_file(module_url, module_path)

    script_path = work_dir / "install-proxy-datadog.sh"
    download_file(script_url, script_path)

    # Package and sign
    tarball_path = work_dir / f"proxy-configurator-{arch}.tgz"
    package(module_path, out=tarball_path)
    sign_package(tarball_path)
    return tarball_path, script_path


def release_installer(args) -> int:
    """
    This subcommand function downloads installer artifacts from the release workflow,
    publishes them to the Gitlab Package Registry, and creates GitLab + Github releases.

    Requirements:
        - You must be logged into the GitHub CLI (gh) to authenticate and publish the release.
        - You must have a GitLab access token with appropriate permissions
    """
    jobs = get_workflow_jobs(args.workflow_id)
    if not jobs:
        return 1

    package_urls = []
    with tempfile.TemporaryDirectory() as work_dir:
        for job in jobs:
            if job["name"].startswith("build installer "):
                # name should be something like "build installer on arm64"
                match = re.match(r"build installer on (amd64|arm64)",
                                 job["name"])
                if match is None:
                    raise Exception(
                        f'Job name does not match regex "{re}": {job}')
                arch = match.groups()[0]
                tar_path, script_path = prepare_installer_release_artifact(
                    Path(work_dir), job["job_number"], arch)

                download_url = upload_to_package_registry(str(tar_path), "proxy-configurator",
                                                          args.version_tag)
                package_urls.append(download_url)

                download_url = upload_to_package_registry(str(script_path), "proxy-configurator",
                                                          args.version_tag)
                package_urls.append(download_url)

                sig_path = str(tar_path) + ".asc"
                if not os.path.exists(sig_path):
                    raise Exception(f'Could not find signature file "{sig_path}"')

                download_url = upload_to_package_registry(sig_path, "proxy-configurator",
                                                          args.version_tag)
                package_urls.append(download_url)

        pubkey_file = os.path.join(work_dir, "pubkey.gpg")
        run([gpg_exe, "--output", pubkey_file, "--armor", "--export"],
            check=True)
        download_url = upload_to_package_registry(pubkey_file, "proxy-configurator",
                                                  args.version_tag)
        package_urls.append(download_url)

        # We've tgz'd and signed all of our release modules.
        # Now let's send them to GitHub in a release via `gh release create`.
        release_files = itertools.chain(
            Path(work_dir).glob("*.tgz"),
            Path(work_dir).glob("*.tgz.asc"),
            Path(work_dir).glob("*.sh"),
            (pubkey_file, ),
        )

        command = [
            gh_exe,
            "release",
            "create",
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

        create_gitlab_release(args.version_tag, set(package_urls))

    return 0


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Build and publish a release of RUM injection installer.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument("--version-tag", )
    parser.add_argument("--ci-token", help="Circle CI Token", required=True)
    parser.add_argument("--gitlab-token", help="GitLab API Token", required=True)
    parser.add_argument(
        "workflow_id",
        type=str,
        help=
        "ID of the release workflow. Find in job url. Example: https://app.circleci.com/pipelines/github/DataDog/inject-browser-sdk/542/workflows/<WORKFLOW_ID>",
    )

    parser.set_defaults(func=release_installer)
    options = parser.parse_args()

    ci_api_token = options.ci_token
    gitlab_token = options.gitlab_token
    try:
        gh_exe = get_gh()
        gpg_exe = get_gpg()
    except (MissingDependency, ValueError) as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)

    print({
        "gh": gh_exe,
        "ci_token": ci_api_token,
        "gitlab_token": gitlab_token,
        "gpg": gpg_exe,
    })

    sys.exit(options.func(options))
