#!/bin/sh

BIN=$(dirname "$0")

if command -v apt-get >/dev/null 2>&1; then
    exec "$BIN/install_build_tooling_apt.sh" "$@"
elif command -v apk >/dev/null 2>&1; then
    exec "$BIN/install_build_tooling_apk.sh" "$@"
elif command -v yum >/dev/null 2>&1; then
    exec "$BIN/install_build_tooling_yum.sh" "$@"
else
    >&2 printf 'Did not find a supported package manager.\n'
    exit 1
fi
