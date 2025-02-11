# Unless explicitly stated otherwise all files in this repository are licensed
# under the Apache 2.0 License. This product includes software developed at
# Datadog (https://www.datadoghq.com/).
#
# Copyright 2024-Present Datadog, Inc.

"""
IIS namespaced tasks
"""
from __future__ import print_function
import os
import shutil
import sys

from invoke import task
from invoke.exceptions import Exit
from .buildutils import do_get_build_env


installer_proj_file = os.path.join(
    "injector_IIS_installer", "injector_IIS_installer.wixproj"
)

PROJECT_DIR = os.path.dirname(os.path.dirname(os.path.realpath(__file__)))


@task
def build(ctx, out_dir="../build_out", debug=False, vstudio_root=None, test_dir=None):
    """
    Build the IIS RUM module
    """

    if sys.platform != "win32":
        print(
            "This task builds the IIS RUM injector for windows, and must be built on Windows\n"
        )
        raise Exit(code=1)

    if not os.path.exists(out_dir):
        os.mkdir(out_dir)

    if not test_dir:
        test_dir = os.path.join(PROJECT_DIR, "test")

    # NOTE(@dmehala): Escape path
    test_dir = test_dir.replace("\\", "/")

    env, fullver, vs_env_bat = do_get_build_env(ctx, vstudio_root=vstudio_root)

    ## Configure and Build the project
    build_type = "Debug" if debug else "RelWithDebInfo"
    cmake_configure_cmd = f"cmake -B build -DCMAKE_BUILD_TYPE={build_type} -DIIS_INJECTOR_STATIC_CRT=1 -DIIS_BUILD_TESTS=1 -G Ninja ."

    result = ctx.run(
        f'call "{vs_env_bat}" -arch=amd64 && {cmake_configure_cmd}',
        env=env,
        hide=False,
        pty=False,
    )

    cmake_build_cmd = "cmake --build build -j 4"
    result = ctx.run(
        f'call "{vs_env_bat}" -arch=amd64 && {cmake_build_cmd}',
        env=env,
        hide=False,
        pty=False,
    )

    testpath = os.path.join(PROJECT_DIR, "build", "test", "unittests", "iis_injector_tests.exe")
    if not os.path.exists(testpath):
        print(f"Could not find unit tests at {testpath}")
        raise Exit(code=1)
    shutil.copy2(testpath, out_dir)

    dllpath = os.path.join(PROJECT_DIR, "build", "iis_injector.dll")
    if not os.path.exists(dllpath):
        print(f"Could not find the injector DLL at {dllpath}")
        raise Exit(code=1)

    ## sign the resulting injection DLL
    sign_binary(ctx, dllpath)

    package(ctx, out_dir, iis_dll=dllpath, debug=debug, vstudio_root=vstudio_root)


@task
def package(ctx, out_dir="../build_out", iis_dll="", debug=False, vstudio_root=None):
    """
    Build the IIS RUM Installer
    """

    if sys.platform != "win32":
        print(
            "This task builds the IIS RUM injector for windows, and must be built on Windows\n"
        )
        raise Exit(code=1)

    if not os.path.exists(out_dir):
        os.mkdir(out_dir)

    if not iis_dll:
        print("Missing dll path")
        raise Exit(code=1)

    env, fullver, vs_env_bat = do_get_build_env(ctx, vstudio_root=vstudio_root)

    ## sign the powershell add-on script.  This is a bit weird, because it will sign the file in place, resulting in
    ## a changed file.  In the CI this is ok, because it's not checked in.  Could lead to issues if this is run locally
    sign_binary(ctx, os.path.join(".", "resources", "modules", "Datadog.RUM.psm1"))
    sign_binary(ctx, os.path.join(".", "resources", "modules", "Datadog.RUM.psd1"))

    # copy the dll
    dllpath = os.path.join(out_dir, "injector_IIS.dll")
    print(f"Copy {iis_dll} to {dllpath}")
    shutil.copy2(iis_dll, dllpath)

    dllpath = dllpath.replace("\\", "/")

    # clean installer obj
    shutil.rmtree(os.path.join("injector_IIS_installer", "obj"), ignore_errors=True)
    shutil.rmtree(os.path.join("injector_IIS_installer", "bin"), ignore_errors=True)

    ## build the installer
    build_type = "Debug" if debug else "Release"
    # NOTE(@dmehala): `OUT_DIR` is a special env var used by msbuild. For some unknown reasons, the task
    # argument is also passed as env var by this framework...
    msi_cmd = f'msbuild {installer_proj_file} -m:4 -p:Configuration={build_type};Platform=x64;InjectorDll="{dllpath}"'

    print(f"Executing: {msi_cmd}")
    result = ctx.run(
        f'call "{vs_env_bat}" -arch=amd64 && {msi_cmd}',
        hide=False,
        pty=False,
        env=env,
    )

    msipath = os.path.join(out_dir, "en-us", "injector_IIS_installer.msi")
    if not os.path.exists(msipath):
        print("Missing installer")
        raise Exit(code=1)

    sign_binary(ctx, msipath)

    ## copy the newly built file into the target directory
    target_filename = "injector_IIS_installer.msi"
    shutil.copy2(msipath, os.path.join(out_dir, target_filename))


def sign_binary(ctx, path):
    if os.environ.get("SIGN_WINDOWS_DD_WCS", False):
        print("Signing {}\n".format(path))
        cmd = f"dd-wcs sign --replace-signature {path}"
        ctx.run(cmd, hide=False, pty=False)
