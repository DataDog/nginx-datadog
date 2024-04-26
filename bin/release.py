#!/usr/bin/env python3
"""Build, test, and publish a GitHub release of this library.
"""

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


def get_git():
    exe_path = shutil.which("git")
    if exe_path is None:
        raise MissingDependency(
            'The "git" command must be available to tag the release commit.')
    return exe_path


def get_tar():
    exe_path = shutil.which("tar")
    if exe_path is None:
        raise MissingDependency(
            'The "tar" command must be available to compress and archive the nginx module.'
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


def download_file(url, destination):
    response = urllib.request.urlopen(url)
    with open(destination, "wb") as output:
        shutil.copyfileobj(response, output)


def parse_info_script(script):
    variables = {}
    for line in script.split("\n"):
        line = line.strip()
        if line == "" or line[0] == "#":
            continue
        assignment, *comment = line.split("#")
        variable, value = assignment.split("=")
        variables[variable] = value
    return variables


def prepare_release_artifact(build_job_number, work_dir):
    artifacts = send_ci_request_paged(
        f"/project/{PROJECT_SLUG}/{build_job_number}/artifacts")
    nginx_version_info_url = None
    module_url = None
    for artifact in artifacts:
        name = artifact["path"]
        if name == "nginx-version-info":
            nginx_version_info_url = artifact["url"]
        elif name == "ngx_http_datadog_module.so":
            module_url = artifact["url"]

    if nginx_version_info_url is None:
        raise Exception(
            f"Job number {build_job_number} doesn't have an 'nginx-version-info' build artifact."
        )
    if module_url is None:
        raise Exception(
            f"Job number {build_job_number} doesn't have an 'ngx_http_datadog_module.so' build artifact."
        )

    nginx_version_info = (
        urllib.request.urlopen(nginx_version_info_url).read().decode("utf8"))
    module_path = work_dir / "ngx_http_datadog_module.so"
    download_file(module_url, module_path)

    # `nginx_version_info` serves to determine the values of BASE_IMAGE and ARCH.
    # These values are instrumental in constructing the tarball name according to
    # the specified convention: <BASE_IMAGE>-<ARCH>-ngx_http_datadog_module.so.tgz
    variables = parse_info_script(nginx_version_info)
    if "BASE_IMAGE" not in variables:
        raise Exception(
            f"BASE_IMAGE not found in nginx-version-info: {nginx_version_info}"
        )
    if "ARCH" not in variables:
        raise Exception(
            f"ARCH not found in nginx-version-info: {nginx_version_info}")

    arch = variables["ARCH"]
    base_prefix = variables["BASE_IMAGE"].replace(":", "_")
    tarball_path = work_dir / f"{base_prefix}-{arch}-ngx_http_datadog_module.so.tgz"
    command = [tar_exe, "-czf", tarball_path, "-C", work_dir, module_path.name]
    run(command, check=True)

    command = [gpg_exe, "--armor", "--detach-sign", tarball_path]
    run(command, check=True)

    module_path.unlink()


def handle_job(job, work_dir):
    # See the response schema for a list of statuses:
    # https://circleci.com/docs/api/v2/index.html#operation/listWorkflowJobs
    if job["name"].startswith("build "):
        prepare_release_artifact(job["job_number"], work_dir)
    return "done"


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Build and publish a release of nginx-datadog.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument("--version-tag", )
    parser.add_argument("--ci-token", help="Circle CI Token", required=True)
    parser.add_argument(
        "workflow_id",
        type=str,
        help=
        "ID of the release workflow. Find in job url. Example: https://app.circleci.com/pipelines/github/DataDog/nginx-datadog/542/workflows/<WORKFLOW_ID>",
    )
    options = parser.parse_args()

    ci_api_token = options.ci_token
    try:
        gh_exe = get_gh()
        gpg_exe = get_gpg()
        git_exe = get_git()
        tar_exe = get_tar()
    except (MissingDependency, ValueError) as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)

    print({
        "token": ci_api_token,
        "gh": gh_exe,
        "gpg": gpg_exe,
        "git": git_exe,
    })

    workflow_id = options.workflow_id
    PROJECT_SLUG = "gh/DataDog/nginx-datadog"

    with tempfile.TemporaryDirectory() as work_dir:
        print("Working directory is", work_dir)

        done_jobs = set()  # job numbers (not IDs)
        jobs = send_ci_request_paged(f"/workflow/{workflow_id}/job")

        # Make sure all jobs run successfully
        for job in jobs:
            if job["status"] != "success":
                print("Found unsuccessful jobs")
                sys.exit(1)

        for job in jobs:
            result = handle_job(job, Path(work_dir))
            if result != "done":
                sys.exit(1)

        pubkey_file = os.path.join(work_dir, "pubkey.gpg")
        run([gpg_exe, "--output", pubkey_file, "--armor", "--export"],
            check=True)

        # We've tgz'd and signed all of our release modules.
        # Now let's send them to GitHub in a release via `gh release create`.
        release_files = itertools.chain(
            Path(work_dir).glob("*.tgz"),
            Path(work_dir).glob("*.tgz.asc"),
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
    sys.exit(0)
