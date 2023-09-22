#!/usr/bin/env python3
"""Build, test, and publish a GitHub release of this library.
"""

import argparse
import itertools
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import time
import urllib.request


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


def get_token():
    name = "CIRCLE_CI_API_TOKEN"
    token = os.environ.get(name)
    if token is None:
        raise MissingDependency(
            f'{name} must be set in the environment. Note that it must be a "personal" API token.'
        )
    return token


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


def get_tar():
    exe_path = shutil.which("tar")
    if exe_path is None:
        raise MissingDependency(
            'The "tar" command must be available to compress and archive the nginx module.'
        )
    return exe_path


def validate_version_tag(tag):
    pattern = r"v[0-9]+\.[0-9]+\.[0-9]+"
    if re.fullmatch(pattern, tag) is None:
        raise ValueError(f"Tag does not match the regular expression /{pattern}/.")
    return tag


def parse_options():
    parser = argparse.ArgumentParser(
        description="Build and publish a release of nginx-datadog.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument(
        "--remote",
        type=str,
        default="origin",
        help="local name of the git remote that points to GitHub",
    )
    parser.add_argument(
        "--no-tag", action="store_true", help="don't tag the current HEAD"
    )
    parser.add_argument(
        "--pipeline-id", type=str, help="use an already-started CircleCI pipeline"
    )
    parser.add_argument(
        "version_tag", help='git tag to associate with the release (e.g. "v1.2.3")'
    )
    return parser.parse_args()


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
        f"/project/{PROJECT_SLUG}/{build_job_number}/artifacts"
    )
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
        urllib.request.urlopen(nginx_version_info_url).read().decode("utf8")
    )
    module_path = work_dir / "ngx_http_datadog_module.so"
    download_file(module_url, module_path)

    # `nginx_version_info` will define BASE_IMAGE. That's that value that we
    # want to prefix the tarball name with, but with colons replaced by
    # underscores.
    variables = parse_info_script(nginx_version_info)
    if "BASE_IMAGE" not in variables:
        raise Exception(
            f"BASE_IMAGE not found in nginx-version-info: {nginx_version_info}"
        )
    if "ARCH" not in variables:
        raise Exception(f"ARCH not found in nginx-version-info: {nginx_version_info}")
    arch = variables["arch"]
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
    status = job["status"]
    if status in (
        "not_run",
        "failed",
        "infrastructure_fail",
        "timedout",
        "terminated-unknown",
        "canceled",
        "unauthorized",
    ):
        raise Exception(f'Job has fatal status "{status}": {job}')
    elif status == "success":
        if job["name"].startswith("build "):
            prepare_release_artifact(job["job_number"], work_dir)
        return "done"


if __name__ == "__main__":
    try:
        options = parse_options()
        options.version_tag = validate_version_tag(options.version_tag)
        ci_api_token = get_token()
        gh_exe = get_gh()
        gpg_exe = get_gpg()
        git_exe = get_git()
        tar_exe = get_tar()
    except (MissingDependency, ValueError) as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)

    print(
        {
            "token": ci_api_token,
            "gh": gh_exe,
            "gpg": gpg_exe,
            "git": git_exe,
            "tag": options.version_tag,
            "remote": options.remote,
            "no-tag": options.no_tag,
        }
    )

    if not options.no_tag:
        command = [git_exe, "ls-remote", "--tags", options.remote, options.version_tag]
        result = run(command, stdout=subprocess.PIPE, check=True)
        if result.stdout:
            raise Exception(
                f"Tag {options.version_tag} already exists on {options.remote}."
            )

        command = [
            git_exe,
            "tag",
            "-a",
            options.version_tag,
            "-m",
            f"release ${options.version_tag}",
        ]
        run(command, check=True)

        command = [git_exe, "push", options.remote, options.version_tag]
        run(command, check=True)

    PROJECT_SLUG = "gh/DataDog/nginx-datadog"

    # Kick off a CircleCI pipeline.
    if options.pipeline_id is not None:
        pipeline_id = options.pipeline_id
    else:
        body = {"tag": options.version_tag}
        _, response = send_ci_request(
            f"/project/{PROJECT_SLUG}/pipeline", payload=body, method="POST"
        )
        print(response)
        pipeline_id = response.get("id")
        if pipeline_id is None:
            raise Exception(
                f'POST [...]/pipeline response did not contain pipeline "id": {response}'
            )

    # TODO: Poll until the number of "build-and-test-all" jobs changes from
    # zero.
    delay_seconds = 20
    print(f"sleeping for {delay_seconds} seconds...")
    time.sleep(delay_seconds)

    # Fetch the pipeline's information.  This will contain its workflows.
    # We're looking for exactly one "build-and-test-all" workflow.
    workflows = send_ci_request_paged(f"/pipeline/{pipeline_id}/workflow")
    workflow = [wf for wf in workflows if wf["name"] == "build-and-test-all"]
    if len(workflow) != 1:
        raise Exception(
            f'Workflows contains the wrong number of "build-and-test-all".  Expected 1 but got {len(workflow)}: {workflows}'
        )
    workflow = workflow[0]
    workflow_id = workflow["id"]

    work_dir = Path(tempfile.mkdtemp())
    print("Working directory is", work_dir)

    # Poll jobs until all are done.
    done_jobs = set()  # job numbers (not IDs)
    while True:
        jobs = send_ci_request_paged(f"/workflow/{workflow_id}/job")
        for job in jobs:
            if job["status"] in ("blocked", "not_running"):
                continue
            job_number = job["job_number"]
            if job_number not in done_jobs:
                result = handle_job(job, work_dir)
                if result == "done":
                    done_jobs.add(job_number)

        if len(done_jobs) == len(jobs):
            break

        sleep_seconds = 20
        print(f"Going to sleep for {sleep_seconds} seconds before checking jobs again.")
        time.sleep(sleep_seconds)

    # We've tgz'd and signed all of our release modules.
    # Now let's send them to GitHub in a release via `gh release create`.
    release_files = itertools.chain(work_dir.glob("*.tgz"), work_dir.glob("*.tgz.asc"))
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
    # run(command, check=True)

    print("removing", work_dir)
    # shutil.rmtree(work_dir)
