#!/bin/sh

repo=$(dirname "$0")/..
nginx_tag=$(cat "$repo"/nginx-tag)
built_image="nginx-datadog-build-$nginx_tag"
BUILD_IMAGE="${BUILD_IMAGE:-$built_image}"

interactive_flags=''
if tty --silent; then
  interactive_flags='--interactive --tty'
fi

docker run \
    $interactive_flags \
    --rm \
    --mount "type=bind,source=$(pwd),destination=/mnt/repo" \
    "$BUILD_IMAGE" \
    "$@"
