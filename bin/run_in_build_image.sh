#!/bin/sh

BUILD_IMAGE=${BUILD_IMAGE:-nginx-datadog-build}

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
