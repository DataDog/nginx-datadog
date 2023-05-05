#!/bin/sh

# Print out a snippet that will go in .circleci/config.yaml
#
# It describes a "build" and "test" job for each supported nginx tag.

set -e

indentation='    '

REPO=$(dirname "$(dirname "$(realpath "$0")")")
cd "$REPO"

# Here are the supported nginx tags.
version_table=$(mktemp)
# base-image    nginx-version    nginx-modules-path    nginx-conf-path    always-build?
>"$version_table" cat <<END_NGINX_TAGS
amazonlinux:2.0.20230418.0 1.22.1 /usr/share/nginx/modules /etc/nginx/nginx.conf always
amazonlinux:2.0.20230320.0 1.22.1 /usr/share/nginx/modules /etc/nginx/nginx.conf
amazonlinux:2.0.20230307.0 1.22.1 /usr/share/nginx/modules /etc/nginx/nginx.conf
amazonlinux:2.0.20230221.0 1.22.1 /usr/share/nginx/modules /etc/nginx/nginx.conf
amazonlinux:2.0.20230207.0 1.22.1 /usr/share/nginx/modules /etc/nginx/nginx.conf
amazonlinux:2.0.20230119.1 1.22.1 /usr/share/nginx/modules /etc/nginx/nginx.conf
amazonlinux:2.0.20221210.0 1.22.1 /usr/share/nginx/modules /etc/nginx/nginx.conf
amazonlinux:2.0.20221103.3 1.22.1 /usr/share/nginx/modules /etc/nginx/nginx.conf
amazonlinux:2.0.20221004.0 1.22.1 /usr/share/nginx/modules /etc/nginx/nginx.conf
amazonlinux:2.0.20220912.1 1.22.1 /usr/share/nginx/modules /etc/nginx/nginx.conf
amazonlinux:2.0.20220805.0 1.22.1 /usr/share/nginx/modules /etc/nginx/nginx.conf
amazonlinux:2.0.20220719.0 1.22.1 /usr/share/nginx/modules /etc/nginx/nginx.conf
amazonlinux:2.0.20220606.1 1.22.1 /usr/share/nginx/modules /etc/nginx/nginx.conf
amazonlinux:2.0.20220426.0 1.22.1 /usr/share/nginx/modules /etc/nginx/nginx.conf
amazonlinux:2.0.20220419.0 1.22.1 /usr/share/nginx/modules /etc/nginx/nginx.conf
amazonlinux:2.0.20220406.1 1.22.1 /usr/share/nginx/modules /etc/nginx/nginx.conf
amazonlinux:2.0.20220316.0 1.22.1 /usr/share/nginx/modules /etc/nginx/nginx.conf
amazonlinux:2.0.20220218.1 1.22.1 /usr/share/nginx/modules /etc/nginx/nginx.conf
amazonlinux:2.0.20220121.0 1.22.1 /usr/share/nginx/modules /etc/nginx/nginx.conf
nginx:1.24.0-alpine 1.23.4 /usr/lib/nginx/modules /etc/nginx/nginx.conf always
nginx:1.24.0 1.23.4 /usr/lib/nginx/modules /etc/nginx/nginx.conf always
nginx:1.23.4-alpine 1.23.4 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.23.4 1.23.4 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.23.3-alpine 1.23.3 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.23.3 1.23.3 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.23.2-alpine 1.23.2 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.23.2 1.23.2 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.23.1-alpine 1.23.1 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.23.1 1.23.1 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.23.0-alpine 1.23.0 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.23.0 1.23.0 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.22.1-alpine 1.22.1 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.22.1 1.22.1 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.22.0-alpine 1.22.0 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.22.0 1.22.0 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.21.6-alpine 1.21.6 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.21.6 1.21.6 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.21.5-alpine 1.21.5 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.21.5 1.21.5 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.21.4-alpine 1.21.4 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.21.4 1.21.4 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.21.3-alpine 1.21.3 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.21.3 1.21.3 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.21.1-alpine 1.21.1 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.21.1 1.21.1 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.21.0-alpine 1.21.0 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.21.0 1.21.0 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.20.2-alpine 1.20.2 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.20.2 1.20.2 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.20.1-alpine 1.20.1 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.20.1 1.20.1 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.20.0-alpine 1.20.0 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.20.0 1.20.0 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.19.10-alpine 1.19.10 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.19.10 1.19.10 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.19.9-alpine 1.19.9 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.19.9 1.19.9 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.19.8-alpine 1.19.8 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.19.8 1.19.8 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.19.7-alpine 1.19.7 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.19.7 1.19.7 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.19.6-alpine 1.19.6 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.19.6 1.19.6 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.19.5-alpine 1.19.5 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.19.5 1.19.5 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.19.4-alpine 1.19.4 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.19.4 1.19.4 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.19.3-alpine 1.19.3 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.19.3 1.19.3 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.19.2-alpine 1.19.2 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.19.2 1.19.2 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.19.1-alpine 1.19.1 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.19.1 1.19.1 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.19.0-alpine 1.19.0 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.19.0 1.19.0 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.18.0-alpine 1.18.0 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.18.0 1.18.0 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.17.10-alpine 1.17.10 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.17.10 1.17.10 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.17.9-alpine 1.17.9 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.17.9 1.17.9 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.17.8-alpine 1.17.8 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.17.8 1.17.8 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.17.7-alpine 1.17.7 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.17.7 1.17.7 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.17.6-alpine 1.17.6 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.17.6 1.17.6 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.17.5-alpine 1.17.5 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.17.5 1.17.5 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.17.4-alpine 1.17.4 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.17.4 1.17.4 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.17.3-alpine 1.17.3 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.17.3 1.17.3 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.17.2-alpine 1.17.2 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.17.2 1.17.2 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.17.1-alpine 1.17.1 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.17.1 1.17.1 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.17.0-alpine 1.17.0 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.17.0 1.17.0 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.16.1-alpine 1.16.1 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.16.1 1.16.1 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.16.0-alpine 1.16.0 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.16.0 1.16.0 /usr/lib/nginx/modules /etc/nginx/nginx.conf
nginx:1.14.1 1.14.1 /usr/lib/nginx/modules /etc/nginx/nginx.conf
END_NGINX_TAGS

while read -r base_image nginx_version nginx_modules_path nginx_conf_path always; do
  base_image_without_colons=$(echo "$base_image" | tr ':' '_')
  filters=$(cat <<'END_FILTERS'
    filters:
      tags:
        only: /^v[0-9]+\.[0-9]+\.[0-9]+/
END_FILTERS
)
  if [ "$always" != 'always' ]; then
    filters=$(cat <<END_FILTERS
$filters
      branches:
        ignore: /.*/
END_FILTERS
)
  fi
  cat <<END_SNIPPET
- build:
    name: "build on $base_image"
    base-image: "$base_image"
    build-image: "datadog/docker-library:nginx-datadog-build-$base_image_without_colons"
    nginx-version: "$nginx_version"
$filters
- test:
    name: "test on $base_image"
    base-image: "$base_image"
    nginx-modules-path: "$nginx_modules_path"
    nginx-conf-path: "$nginx_conf_path"
    requires:
    - "build on $base_image"
$filters
END_SNIPPET
done <"$version_table" | sed "s/^/$indentation/"

rm "$version_table"
