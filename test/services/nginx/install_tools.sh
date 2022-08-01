#!/bin/sh

set -e

if command -v apt-get >/dev/null 2>&1; then
    apt-get update
    # `procps` contains `kill`, which is used to bring down temporary instances
    # of nginx.
    DEBIAN_FRONTEND=noninteractive apt-get install -y procps
elif command -v apk >/dev/null 2>&1; then
    apk update
    # `procps` contains `kill`, which is used to bring down temporary instances
    # of nginx.
    apk add procps
else
    >&2 printf 'Did not find a supported package manager.\n'
    exit 1
fi
