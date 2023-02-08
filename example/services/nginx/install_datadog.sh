#!/bin/sh

set -x
set -e

if [ "$BASE_IMAGE" = '' ]; then
  >&2 echo 'This script expects $BASE_IMAGE to be in the environment, e.g. "nginx:1.23.1-alpine" or "amazonlinux:2.0.20220121.0".'
  exit 1
fi

case "$BASE_IMAGE" in
    amazonlinux:*) nginx_modules_path=/usr/share/nginx/modules ;;
    *) nginx_modules_path=/usr/lib/nginx/modules ;;
esac

# If nginx itself is not installed already, then install it.
if ! command -v nginx >/dev/null 2>&1; then
  if command -v apt-get >/dev/null 2>&1; then
      apt-get update
      DEBIAN_FRONTEND=noninteractive apt-get install -y nginx
  elif command -v apk >/dev/null 2>&1; then
      apk update
      apk add nginx
  elif command -v yum >/dev/null 2>&1; then
      yum update -y
      amazon-linux-extras enable -y nginx1
      yum install -y nginx
  else
      >&2 printf 'Did not find a supported package manager.\n'
      exit 2
  fi
fi

# Install the command line tools needed to fetch and extract the module.
# `apt-get` (Debian, Ubuntu), `apk` (Alpine), and `yum` (Amazon Linux) are
# supported.
if command -v apt-get >/dev/null 2>&1; then
    apt-get update
    DEBIAN_FRONTEND=noninteractive apt-get install -y tar wget curl jq
elif command -v apk >/dev/null 2>&1; then
    apk update
    apk add tar wget curl jq
elif command -v yum >/dev/null 2>&1; then
    yum update -y
    yum install -y tar wget curl jq
else
    >&2 printf 'Did not find a supported package manager.\n'
    exit 3
fi

get_latest_release() {
  curl --silent "https://api.github.com/repos/$1/releases/latest" | jq --raw-output .tag_name
}

RELEASE_TAG=$(get_latest_release DataDog/nginx-datadog)

base_image_without_colons=$(echo "$BASE_IMAGE" | tr ':' '_')
tarball="$base_image_without_colons-ngx_http_datadog_module.so.tgz"
wget "https://github.com/DataDog/nginx-datadog/releases/download/$RELEASE_TAG/$tarball"
tar -xzf "$tarball" -C "$nginx_modules_path"
rm "$tarball"
