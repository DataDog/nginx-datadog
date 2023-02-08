#!/bin/sh

repo=$(dirname "$0")/..
. "$repo/nginx-version-info"
base_image_without_colons=$(echo "$BASE_IMAGE" | tr ':' '_')
built_image="nginx-datadog-build-$base_image_without_colons"
BUILD_IMAGE="${BUILD_IMAGE:-$built_image}"

interactive_flags=''
if tty --silent; then
  interactive_flags='--interactive --tty'
fi

# shellcheck disable=SC2086
docker run \
    $interactive_flags \
    --rm \
    --mount "type=bind,source=$(pwd),destination=/mnt/repo" \
    "$BUILD_IMAGE" \
    "$@"
