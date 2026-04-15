#!/bin/sh
# Install the Nginx Datadog module.

set -x
set -e

. /tmp/install_datadog_utils.sh

detect_arch

install_packages curl jq tar wget

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
