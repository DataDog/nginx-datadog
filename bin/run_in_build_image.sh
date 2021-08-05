#!/bin/sh

docker run \
    --interactive \
    --tty \
    --mount "type=bind,source=$(pwd),destination=/mnt/repo" \
    nginx-module-cmake-build \
    "$@"
