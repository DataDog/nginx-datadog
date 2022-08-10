#!/usr/bin/env python3
"""TODO - Document this
"""

import argparse
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import urllib.request


class MissingDependency(Exception):
    pass


def get_token():
    name = 'CIRCLE_CI_API_TOKEN'
    token = os.environ.get(name)
    if token is None:
        raise MissingDependency(
            f'{name} must be set in the environment. Note that it must be a "personal" API token.'
        )
    return token


def get_gh():
    exe_path = shutil.which('gh')
    if exe_path is None:
        raise MissingDependency(
            'The "gh" command must be available to publish a release to GitHub.  Installation instructions are available at <https://cli.github.com/>.'
        )
    return exe_path


def get_gpg():
    exe_path = shutil.which('gpg')
    if exe_path is None:
        raise MissingDependency(
            'The "gpg" command must be available and configured to be able to create detached signatures of release artifacts.'
        )
    return exe_path


def get_git():
    exe_path = shutil.which('git')
    if exe_path is None:
        raise MissingDependency(
            'The "git" command must be available to tag the release commit.')
    return exe_path


def validate_version_tag(tag):
    pattern = r'v[0-9]+\.[0-9]+\.[0-9]+'
    if re.fullmatch(pattern, tag) is None:
        raise ValueError(
            f'Tag does not match the regular expression /{pattern}/.')
    return tag


def parse_options(args):
    parser = argparse.ArgumentParser(
        description='Build and publish a release of nginx-datadog.',
        formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser.add_argument(
        '--remote',
        type=str,
        default='origin',
        help='local name of the git remote that points to GitHub')
    parser.add_argument('--no-tag',
                        action='store_true',
                        help="don't tag the current HEAD")
    parser.add_argument('--pipeline-id',
                        type=str,
                        help='use an already-started CircleCI pipeline')
    parser.add_argument(
        'version_tag',
        help='git tag to associate with the release (e.g. "v1.2.3")')
    return parser.parse_args()


try:
    ci_api_token = get_token()
    gh_exe = get_gh()
    gpg_exe = get_gpg()
    git_exe = get_git()
    options = parse_options(sys.argv[1:])
    options.version_tag = validate_version_tag(options.version_tag)
except (MissingDependency, ValueError) as error:
    print(str(error), file=sys.stderr)
    sys.exit(1)

print({
    'token': ci_api_token,
    'gh': gh_exe,
    'gpg': gpg_exe,
    'git': git_exe,
    'tag': options.version_tag,
    'remote': options.remote,
    'no-tag': options.no_tag
})


def run(command, *args, **kwargs):
    print('+', shlex.join(command), file=sys.stderr)
    return subprocess.run(command, *args, **kwargs)


if not options.no_tag:
    command = [
        git_exe, 'ls-remote', '--tags', options.remote, options.version_tag
    ]
    result = run(command, stdout=subprocess.PIPE, check=True)
    if result.stdout:
        raise Exception(
            f'Tag {options.version_tag} already exists on {options.remote}.')

    command = [
        git_exe, 'tag', '-a', options.version_tag, '-m',
        f'release ${options.version_tag}'
    ]
    run(command, check=True)

    command = [git_exe, 'push', options.remote, options.version_tag]
    run(command, check=True)

PROJECT_URL = 'https://circleci.com/api/v2/project/gh/DataDog/nginx-datadog'


def send_ci_request(path, payload=None, method=None):
    headers = {'Circle-Token': ci_api_token}
    if payload is not None:
        headers['Content-Type'] = 'application/json; charset=utf-8'
        payload = json.dumps(payload).encode('utf8')

    url = f'{PROJECT_URL}{path}'
    request = urllib.request.Request(url,
                                     data=payload,
                                     headers=headers,
                                     method=method)
    print('+', request.get_method(), request.full_url, request.data or '')

    response = urllib.request.urlopen(request)
    response_body = json.load(response)
    if response.status < 200 or response.status > 299:
        raise Exception(
            f'HTTP error response {response.status}: {response_body}')
    return response.status, response_body


# Kick off a CircleCI pipeline.
if options.pipeline_id is not None:
    pipeline_id = options.pipeline_id
else:
    body = {"tag": options.version_tag}
    _, response = send_ci_request('/pipeline', payload=body, method='POST')
    pipeline_id = response.get('id')
    if pipeline_id is None:
        raise Exception(
            f'POST [...]/pipeline response did not contain pipeline "id": {response}'
        )

# Fetch the pipeline's information.  This will contain its jobs.
_, response = send_ci_request(f'/pipeline/{pipeline_id}')
print(response)

# TODO
#
# x tag the current head
# - kick off pipeline
# - get workflows
# - extrace "build-and-test-all" workflow
# - poll workflow's jobs until all are successful
# - for each build job, download artifacts
# - .tgz with GPG detached signature
# - use "gh"
