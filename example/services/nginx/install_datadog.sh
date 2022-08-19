#!/bin/sh

set -e

if command -v apt-get >/dev/null 2>&1; then
    apt-get update
    DEBIAN_FRONTEND=noninteractive apt-get install -y tar wget curl jq
elif command -v apk >/dev/null 2>&1; then
    apk update
    apk add tar wget curl jq
else
    >&2 printf 'Did not find a supported package manager.\n'
    exit 1
fi

if [ "$NGINX_IMAGE_TAG" = '' ]; then
  >&2 echo 'This script expects $NGINX_IMAGE_TAG to be in the environment, e.g. "1.23.1" or "1.19.0-alpine".'
  exit 2
fi

get_latest_release() {
  curl --silent "https://api.github.com/repos/$1/releases/latest" | jq --raw-output .tag_name
}

RELEASE_TAG=$(get_latest_release DataDog/nginx-datadog)

tarball="$NGINX_IMAGE_TAG-ngx_http_datadog_module.so.tgz"
wget "https://github.com/DataDog/nginx-datadog/releases/download/$RELEASE_TAG/$tarball"
tar -xzf "$tarball" -C /usr/lib/nginx/modules
rm "$tarball"
