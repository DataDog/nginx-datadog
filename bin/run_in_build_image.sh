#!/bin/sh

repo=$(dirname "$0")/..
# shellcheck disable=SC1091
. "$repo/nginx-version-info"
base_image_without_colons=$(echo "$BASE_IMAGE" | tr ':' '_')
built_image="nginx-datadog-build-$base_image_without_colons"
BUILD_IMAGE="${BUILD_IMAGE:-$built_image}"

interactive_flags=''
if tty -s; then
  interactive_flags='--interactive --tty'
fi

# If the image doesn't exist locally, try DockerHub instead.
if [ "$(docker images -q "$BUILD_IMAGE:latest" 2>/dev/null)" = '' ]; then
  BUILD_IMAGE=datadog/docker-library:$BUILD_IMAGE
fi

# shellcheck disable=SC2086
docker run \
    $interactive_flags \
    --rm \
    --mount "type=bind,source=$(pwd),destination=/mnt/repo" \
    "$BUILD_IMAGE" \
    "$@"
