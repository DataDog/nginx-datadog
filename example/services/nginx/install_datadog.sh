#!/bin/sh
# Set up the nginx container image. Install nginx, if necessary, and install the nginx-datadog module.

set -x
set -e

is_installed() {
  command -v "$1" > /dev/null 2>&1
}

get_latest_release() {
  curl --silent "https://api.github.com/repos/$1/releases/latest" | jq --raw-output .tag_name
}

if [ "$BASE_IMAGE" = '' ]; then
  >&2 echo 'This script expects BASE_IMAGE to be in the environment, e.g. "nginx:1.23.1-alpine" or "amazonlinux:2.0.20220121.0".'
  exit 1
fi

# Fail fast in case the base image is not supported.
# For now, we only support nginx, nginx-alpine, and amazonlinux images.
case "$BASE_IMAGE" in
    amazonlinux:*)
      nginx_modules_path=/usr/share/nginx/modules
      ;;
    nginx:*)
      nginx_modules_path=/usr/lib/nginx/modules
      ;;
    *)
      >&2 printf 'BASE_IMAGE value "%s" is invalid. See nginx-version-info.example for more information.\n' "$BASE_IMAGE"
      exit 1
      ;;
esac

ARCH="$(uname -m)"
case "$ARCH" in
    aarch64)
      ARCH="arm64"
      ;;
    x86_64)
      ARCH="amd64"
      ;;
    *)
      >&2 echo "Platform ${BASE_IMAGE}-${ARCH} is not supported."
      exit 1
      ;;
esac

# Install the command line tools needed to fetch and extract the module.
# `apt-get` (Debian, Ubuntu), `apk` (Alpine), and `yum` (Amazon Linux) are
# supported.
if is_installed apt-get; then
    apt-get update
    DEBIAN_FRONTEND=noninteractive apt-get install -y tar wget curl jq
elif is_installed apk; then
    apk update
    apk add tar wget curl jq
elif is_installed yum; then
    yum update -y
    yum install -y tar wget curl jq
else
    >&2 printf 'Did not find a supported package manager.\n'
    exit 3
fi

# If nginx itself is not installed already, then install it.
if ! is_installed nginx; then
  if is_installed apt-get; then
      apt-get update
      DEBIAN_FRONTEND=noninteractive apt-get install -y nginx
  elif is_installed apk; then
      apk update
      apk add nginx
  elif is_installed yum; then
      yum update -y
      # Older versions of Amazon Linux needed "amazon-linux-extras" in order to
      # install nginx. Newer versions of Amazon Linux don't have
      # "amazon-linux-extras".
      if >/dev/null command -v amazon-linux-extras; then
          amazon-linux-extras enable -y nginx1
      fi
      yum install -y nginx
  else
      >&2 printf 'Did not find a supported package manager.\n'
      exit 2
  fi
fi

# TODO(@dmehala): nginx version can be different from the base image
RELEASE_TAG=$(get_latest_release DataDog/nginx-datadog)

base_image_without_colons=$(echo "$BASE_IMAGE" | tr ':' '_')
tarball="${base_image_without_colons}-${ARCH}-ngx_http_datadog_module.so.tgz"

wget "https://github.com/DataDog/nginx-datadog/releases/download/$RELEASE_TAG/$tarball"
tar -xzf "$tarball" -C "$nginx_modules_path"
rm "$tarball"
