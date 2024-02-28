#!/bin/sh

# Print out a snippet that will go in .circleci/config.yaml
#
# It describes a "build" and "test" job for each supported nginx tag.

set -e

indentation='    '

REPO=$(dirname "$(dirname "$(realpath "$0")")")
cd "$REPO"

# Here are the supported build/test configurations.
version_table=$(mktemp)
# base-image    nginx-version    architecture
>"$version_table" cat <<END_NGINX_TAGS
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
nginx:1.25.4-alpine 1.25.4 amd64,arm64
nginx:1.25.4 1.25.4 amd64,arm64
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
nginx:1.21.6-alpine 1.21.6 amd64,arm64
nginx:1.21.6 1.21.6 amd64,arm64
nginx:1.21.5-alpine 1.21.5 amd64,arm64
nginx:1.21.5 1.21.5 amd64,arm64
nginx:1.21.4-alpine 1.21.4 amd64,arm64
nginx:1.21.4 1.21.4 amd64,arm64
nginx:1.21.3-alpine 1.21.3 amd64,arm64
nginx:1.21.3 1.21.3 amd64,arm64
nginx:1.21.1-alpine 1.21.1 amd64,arm64
nginx:1.21.1 1.21.1 amd64,arm64
nginx:1.21.0-alpine 1.21.0 amd64,arm64
nginx:1.21.0 1.21.0 amd64,arm64
nginx:1.20.2-alpine 1.20.2 amd64,arm64
nginx:1.20.2 1.20.2 amd64,arm64
nginx:1.20.1-alpine 1.20.1 amd64,arm64
nginx:1.20.1 1.20.1 amd64,arm64
nginx:1.20.0-alpine 1.20.0 amd64,arm64
nginx:1.20.0 1.20.0 amd64,arm64
nginx:1.19.10-alpine 1.19.10 amd64,arm64
nginx:1.19.10 1.19.10 amd64,arm64
nginx:1.19.9-alpine 1.19.9 amd64,arm64
nginx:1.19.9 1.19.9 amd64,arm64
nginx:1.19.8-alpine 1.19.8 amd64,arm64
nginx:1.19.8 1.19.8 amd64,arm64
nginx:1.19.7-alpine 1.19.7 amd64,arm64
nginx:1.19.7 1.19.7 amd64,arm64
nginx:1.19.6-alpine 1.19.6 amd64,arm64
nginx:1.19.6 1.19.6 amd64,arm64
nginx:1.19.5-alpine 1.19.5 amd64,arm64
nginx:1.19.5 1.19.5 amd64,arm64
nginx:1.19.4-alpine 1.19.4 amd64,arm64
nginx:1.19.4 1.19.4 amd64,arm64
nginx:1.19.3-alpine 1.19.3 amd64,arm64
nginx:1.19.3 1.19.3 amd64,arm64
nginx:1.19.2-alpine 1.19.2 amd64,arm64
nginx:1.19.2 1.19.2 amd64,arm64
nginx:1.19.1-alpine 1.19.1 amd64,arm64
nginx:1.19.1 1.19.1 amd64,arm64
nginx:1.19.0-alpine 1.19.0 amd64
nginx:1.19.0 1.19.0 amd64,arm64
nginx:1.18.0-alpine 1.18.0 amd64,arm64
nginx:1.18.0 1.18.0 amd64,arm64
nginx:1.17.10-alpine 1.17.10 amd64,arm64
nginx:1.17.10 1.17.10 amd64,arm64
nginx:1.17.9-alpine 1.17.9 amd64,arm64
nginx:1.17.9 1.17.9 amd64,arm64
nginx:1.17.8-alpine 1.17.8 amd64,arm64
nginx:1.17.8 1.17.8 amd64,arm64
nginx:1.17.7-alpine 1.17.7 amd64,arm64
nginx:1.17.7 1.17.7 amd64,arm64
nginx:1.17.6-alpine 1.17.6 amd64,arm64
nginx:1.17.6 1.17.6 amd64,arm64
nginx:1.17.5-alpine 1.17.5 amd64,arm64
nginx:1.17.5 1.17.5 amd64,arm64
nginx:1.17.4-alpine 1.17.4 amd64,arm64
nginx:1.17.4 1.17.4 amd64,arm64
nginx:1.17.3-alpine 1.17.3 amd64,arm64
nginx:1.17.3 1.17.3 amd64,arm64
nginx:1.17.2-alpine 1.17.2 amd64,arm64
nginx:1.17.2 1.17.2 amd64,arm64
nginx:1.17.1-alpine 1.17.1 amd64,arm64
nginx:1.17.0-alpine 1.17.0 amd64,arm64
nginx:1.16.1-alpine 1.16.1 amd64,arm64
nginx:1.16.1 1.16.1 amd64,arm64
nginx:1.16.0-alpine 1.16.0 amd64,arm64
END_NGINX_TAGS

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
