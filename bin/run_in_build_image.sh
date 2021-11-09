#!/bin/sh

docker run \
    --interactive \
    --tty \
    --rm \
    --mount "type=bind,source=$(pwd),destination=/mnt/repo" \
    nginx-module-cmake-build \
    "$@"
