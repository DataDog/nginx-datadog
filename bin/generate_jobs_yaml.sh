#!/bin/sh

# Print out a snippet that will go in .circleci/config.yaml
#
# It describes a "build" and "test" job for each supported base docker image.

set -e

indentation='    '

REPO=$(dirname "$(dirname "$(realpath "$0")")")
cd "$REPO"

# Here are the supported build/test configurations.
version_table=$(mktemp)
# base-image    nginx-version    architectures
>"$version_table" cat <<END_TABLE
amazonlinux:2023.3.20240219.0 1.24.0 amd64,arm64
amazonlinux:2.0.20230418.0 1.22.1 amd64,arm64
amazonlinux:2.0.20230320.0 1.22.1 amd64,arm64
amazonlinux:2.0.20230307.0 1.22.1 amd64,arm64
amazonlinux:2.0.20230221.0 1.22.1 amd64,arm64
amazonlinux:2.0.20230207.0 1.22.1 amd64,arm64
amazonlinux:2.0.20230119.1 1.22.1 amd64,arm64
amazonlinux:2.0.20221210.0 1.22.1 amd64,arm64
amazonlinux:2.0.20221103.3 1.22.1 amd64,arm64
amazonlinux:2.0.20221004.0 1.22.1 amd64,arm64
amazonlinux:2.0.20220912.1 1.22.1 amd64,arm64
amazonlinux:2.0.20220805.0 1.22.1 amd64,arm64
amazonlinux:2.0.20220719.0 1.22.1 amd64,arm64
amazonlinux:2.0.20220606.1 1.22.1 amd64,arm64
amazonlinux:2.0.20220426.0 1.22.1 amd64,arm64
amazonlinux:2.0.20220419.0 1.22.1 amd64,arm64
amazonlinux:2.0.20220406.1 1.22.1 amd64,arm64
amazonlinux:2.0.20220316.0 1.22.1 amd64,arm64
amazonlinux:2.0.20220218.1 1.22.1 amd64,arm64
amazonlinux:2.0.20220121.0 1.22.1 amd64,arm64
nginx:1.26.0-alpine 1.26.0 amd64,arm64
nginx:1.26.0 1.26.0 amd64,arm64
nginx:1.25.5-alpine 1.25.5 amd64,arm64
nginx:1.25.5 1.25.5 amd64,arm64
nginx:1.25.4-alpine 1.25.4 amd64,arm64
nginx:1.25.4 1.25.4 amd64,arm64
nginx:1.25.3-alpine 1.25.3 amd64,arm64
nginx:1.25.3 1.25.3 amd64,arm64
nginx:1.25.2-alpine 1.25.2 amd64,arm64
nginx:1.25.2 1.25.2 amd64,arm64
nginx:1.25.1-alpine 1.25.1 amd64,arm64
nginx:1.25.1 1.25.1 amd64,arm64
nginx:1.25.0-alpine 1.25.0 amd64,arm64
nginx:1.25.0 1.25.0 amd64,arm64
nginx:1.24.0-alpine 1.24.0 amd64,arm64
nginx:1.24.0 1.24.0 amd64,arm64
nginx:1.23.4-alpine 1.23.4 amd64,arm64
nginx:1.23.4 1.23.4 amd64,arm64
nginx:1.23.3-alpine 1.23.3 amd64,arm64
nginx:1.23.3 1.23.3 amd64,arm64
nginx:1.23.2-alpine 1.23.2 amd64,arm64
nginx:1.23.2 1.23.2 amd64,arm64
nginx:1.23.1-alpine 1.23.1 amd64,arm64
nginx:1.23.1 1.23.1 amd64,arm64
nginx:1.23.0-alpine 1.23.0 amd64,arm64
nginx:1.23.0 1.23.0 amd64,arm64
nginx:1.22.1-alpine 1.22.1 amd64,arm64
nginx:1.22.1 1.22.1 amd64,arm64
nginx:1.22.0-alpine 1.22.0 amd64,arm64
nginx:1.22.0 1.22.0 amd64,arm64
END_TABLE

while read -r base_image nginx_version archs; do
  base_image_without_colons=$(echo "$base_image" | tr ':' '_')

  cat <<END_SNIPPET
- build:
    <<: *release_tag_only
    matrix:
      parameters:
        arch: [$archs]
    name: "build on $base_image-<< matrix.arch >>"
    base-image: "$base_image"
    build-image: "datadog/docker-library:nginx-datadog-build-$base_image_without_colons"
    nginx-version: "$nginx_version"
- test:
    <<: *release_tag_only
    matrix:
      parameters:
        arch: [$archs]
    requires:
    - "build on $base_image-<< matrix.arch >>"
    name: "test on $base_image-<< matrix.arch >>"
    base-image: "$base_image"
END_SNIPPET
done <"$version_table" | sed "s/^/$indentation/"

rm "$version_table"
