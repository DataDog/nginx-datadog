# Unless explicitly stated otherwise all files in this repository are licensed
# under the Apache 2.0 License. This product includes software developed at
# Datadog (https://www.datadoghq.com/).
#
# Copyright 2024-Present Datadog, Inc.

"""
Miscellaneous functions, no tasks here
"""
from __future__ import print_function

import hashlib
import os
import re
import shutil
import sys
import json
from invoke.exceptions import Exit
from subprocess import check_output


def query_version(ctx, git_sha_length=7, prefix=None, major_version_hint=None):
    # The string that's passed in will look something like this: 6.0.0-beta.0-1-g4f19118
    # if the tag is 6.0.0-beta.0, it has been one commit since the tag and that commit hash is g4f19118
    cmd = "git describe --tags --candidates=50"
    if prefix and type(prefix) == str:
        cmd += " --match \"{}-*\"".format(prefix)
    else:
        if major_version_hint:
            cmd += r' --match "{}\.*"'.format(major_version_hint)
        else:
            cmd += " --match \"[0-9]*\""
    if git_sha_length and type(git_sha_length) == int:
        cmd += " --abbrev={}".format(git_sha_length)
    described_version = ctx.run(cmd, hide=True).stdout.strip()

    # for the example above, 6.0.0-beta.0-1-g4f19118, this will be 1
    commit_number_match = re.match(r"^.*-(?P<commit_number>\d+)-g[0-9a-f]+$", described_version)
    commit_number = 0
    if commit_number_match:
        commit_number = int(commit_number_match.group('commit_number'))

    version_re = r"v?(?P<version>\d+\.\d+\.\d+)(?:(?:-|\.)(?P<pre>[0-9A-Za-z.-]+))?"
    if prefix and type(prefix) == str:
        version_re = r"^(?:{}-)?".format(prefix) + version_re
    else:
        version_re = r"^" + version_re
    if commit_number == 0:
        version_re += r"(?P<git_sha>)$"
    else:
        version_re += r"-\d+-g(?P<git_sha>[0-9a-f]+)$"

    version_match = re.match(version_re, described_version)

    if not version_match:
        raise Exception("Could not query valid version from tags of local git repository")

    # version: for the tag 6.0.0-beta.0, this will match 6.0.0
    # pre: for the output, 6.0.0-beta.0-1-g4f19118, this will match beta.0
    # if there have been no commits since, it will be just 6.0.0-beta.0,
    # and it will match beta.0
    # git_sha: for the output, 6.0.0-beta.0-1-g4f19118, this will match g4f19118
    version, pre, git_sha = version_match.group('version', 'pre', 'git_sha')
    return version, pre, commit_number, git_sha


def get_version(
    ctx, include_git=False, url_safe=False, git_sha_length=7, prefix=None, env=os.environ, major_version='7'
):
    # we only need the git info for the non omnibus builds, omnibus includes all this information by default

    version = ""
    version, pre, commits_since_version, git_sha = query_version(
        ctx, git_sha_length, prefix, major_version_hint=None
    )
    if pre:
        version = "{0}-{1}".format(version, pre)
    if commits_since_version and include_git:
        if url_safe:
            version = "{0}.git.{1}.{2}".format(version, commits_since_version, git_sha)
        else:
            version = "{0}+git.{1}.{2}".format(version, commits_since_version, git_sha)

    # version could be unicode as it comes from `query_version`
    return str(version)


def get_version_numeric_only(ctx, env=os.environ, major_version='7'):
    # we only need the git info for the non omnibus builds, omnibus includes all this information by default

    version, _, _, _ = query_version(ctx, major_version_hint=None)
    return version