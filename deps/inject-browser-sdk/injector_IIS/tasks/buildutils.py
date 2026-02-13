# Unless explicitly stated otherwise all files in this repository are licensed
# under the Apache 2.0 License. This product includes software developed at
# Datadog (https://www.datadoghq.com/).
#
# Copyright 2024-Present Datadog, Inc.

"""
Miscellaneous build helper functions, no tasks here
"""
from __future__ import print_function

import datetime
import os

from invoke.exceptions import Exit
from .utils import get_version_numeric_only, get_version


def do_get_build_env(ctx, vstudio_root=None):
    ver = get_version_numeric_only(ctx, env=os.environ, major_version=None)
    build_maj, build_min, build_patch = ver.split(".")
    fullver = get_version(ctx, include_git=True, url_safe=True, major_version=None)

    env = {
        "MAJ_VER": str(build_maj),
        "MIN_VER": str(build_min),
        "PATCH_VER": str(build_patch),
    }

    vsroot = os.getenv("VSINSTALLDIR")
    if not vsroot:
        print("Visual Studio Not installed in environment; checking other locations")

        vsroot = vstudio_root or os.getenv("VSTUDIO_ROOT")
        if not vsroot:
            print("Must have visual studio installed")
            raise Exit(code=2)

    batchfile = "VsDevCmd.bat"
    vs_env_bat = "{}\\Common7\\Tools\\{}".format(vsroot, batchfile)

    return env, fullver, vs_env_bat
