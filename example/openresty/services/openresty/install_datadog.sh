#!/bin/sh
# Install the Nginx Datadog module.

set -x
set -e

is_installed() {
  command -v "$1" > /dev/null 2>&1
}

get_latest_release() {
  curl --silent "https://api.github.com/repos/$1/releases/latest" | jq --raw-output .tag_name
}

arch="$(uname -m)"
case "$arch" in
    aarch64)
      arch="arm64"
      ;;
    x86_64)
      arch="amd64"
      ;;
    *)
      >&2 echo "Platform ${arch} is not supported."
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

if [ -z "$OPENRESTY_VERSION" ]; then
  >&2 echo 'OPENRESTY_VERSION must be set (e.g. "1.29.2.1").'
  exit 1
fi

tarball="openresty-ngx_http_datadog_module-appsec-${arch}-${OPENRESTY_VERSION}.so.tgz"

repository="DataDog/nginx-datadog"

if [ -n "$NGINX_DATADOG_VERSION" ]; then
  nginx_datadog_release_tag="v${NGINX_DATADOG_VERSION}"
else
  nginx_datadog_release_tag=$(get_latest_release "$repository")
fi

wget "https://github.com/$repository/releases/download/$nginx_datadog_release_tag/$tarball"
tar -xzf "$tarball" -C /usr/local/openresty/nginx/modules
rm "$tarball"
