#!/usr/bin/env python3
"""Print build-relevant info to standard output as JSON

TODO
"""

import argparse
import json
import shlex
import shutil
import subprocess
import sys

DOCKER = shutil.which('docker')

parser = argparse.ArgumentParser(
    description='Extract build info from nginx image')
parser.add_argument('nginx_tag',
                    help='tag of the nginx image, e.g. 1.19.1-alpine')
parser.add_argument('--rm',
                    action='store_true',
                    help='if we have to download the image, remove it after')
options = parser.parse_args()

image = f'nginx:{options.nginx_tag}'

had_to_download_image = False
if options.rm:
    # See whether the image already exists.  If _not_, then `docker run` will
    # download it, and since `--rm` was specified, we'll want to remove it
    # later.
    # We can check if the image exists locally by trying to `docker image
    # inspect` it.
    command = [DOCKER, 'image', 'inspect', image]
    result = subprocess.run(command,
                            stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL)
    if result.returncode != 0:
        had_to_download_image = True

shell_script = r"""
# Deduce the build image from /etc/os-release.

# /etc/os-release defines environment (shell) variables that describe the
# system.
eval "$(cat /etc/os-release)"

# It's either Debian or Alpine.  We can tell which by looking at the beginning
# of $NAME.  Then, either way, the version is $VERSION_ID, except that we
# append "-slim" to the Debian tag (to get a smaller image, as nginx does).
case "$NAME" in
    "Debian "*)   echo "debian:$VERSION_ID-slim" ;;
    "Alpine "*)   echo "alpine:$VERSION_ID" ;;
    *)  >&2 echo "Unexpected nginx image system name: $NAME"
        exit 1 ;;
esac

# Ask nginx for which arguments were passed to its "configure" script when it
# was built.  Lots of shell quoting going on here.
nginx -V 2>&1 | sed -n 's/^configure arguments: \(.*\)/\1/p'
"""

command = [
    DOCKER, 'run', '--interactive', '--rm', '--entrypoint=/bin/sh', image
]
try:
    result = subprocess.run(command,
                            input=shell_script,
                            capture_output=True,
                            check=True,
                            encoding='utf8')
except subprocess.CalledProcessError as error:
    print(
        {
            'returncode': error.returncode,
            'stdout': error.stdout,
            'stderr': error.stderr
        },
        file=sys.stderr)
    sys.exit(1)

base_image, configure_args, *_ = result.stdout.split('\n')

print(
    json.dumps({
        'base_image': base_image,
        'configure_args': shlex.split(configure_args)
    }))

if options.rm and had_to_download_image:
    command = [DOCKER, 'image', 'rm', image]
    subprocess.run(command,
                   stdout=subprocess.DEVNULL,
                   stderr=subprocess.DEVNULL)
