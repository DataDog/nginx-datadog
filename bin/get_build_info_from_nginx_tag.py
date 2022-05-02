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

parser = argparse.ArgumentParser(
    description='Extract build info from nginx image')
parser.add_argument('nginx_tag',
                    help='tag of the nginx image, e.g. 1.19.1-alpine')
nginx_tag = parser.parse_args().nginx_tag

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
    shutil.which('docker'), 'run', '--interactive', '--rm',
    '--entrypoint=/bin/sh', f'nginx:{nginx_tag}'
]
try:
    result = subprocess.run(command,
                            input=shell_script,
                            capture_output=True,
                            check=True,
                            encoding='utf8')
except subprocess.CalledProcessError as error:
    print({
        'returncode': error.returncode,
        'stdout': error.stdout,
        'stderr': error.stderr
    })
    sys.exit(1)

base_image, configure_args, *_ = result.stdout.split('\n')

print(
    json.dumps({
        'base_image': base_image,
        'configure_args': shlex.split(configure_args)
    }))
