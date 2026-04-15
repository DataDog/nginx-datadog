#!/bin/sh
# Set up the Nginx container image. Install Nginx, if necessary, and install the Nginx Datadog module.

set -x
set -e

. /tmp/install_datadog_utils.sh

get_nginx_version() {
  if ! is_installed nginx; then
    >&2 echo 'Could not execute nginx binary'
    exit 1
  fi
  nginx -version 2>&1 | sed 's@.*/@@'
}

if [ "$BASE_IMAGE" = '' ]; then
  >&2 echo 'This script expects BASE_IMAGE to be in the environment, e.g. "nginx:1.29.4-alpine" or "amazonlinux:2.0.20220121.0".'
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

arch=$(detect_arch)

install_packages curl jq tar wget

# If nginx itself is not installed already, then install it.
if ! is_installed nginx; then
  # Older versions of Amazon Linux needed "amazon-linux-extras" in order to
  # install nginx. Newer versions of Amazon Linux don't have
  # "amazon-linux-extras".
  if is_installed yum && >/dev/null command -v amazon-linux-extras; then
      amazon-linux-extras enable -y nginx1
  fi
  install_packages nginx
fi

nginx_version=$(get_nginx_version)
tarball="ngx_http_datadog_module-appsec-${arch}-${nginx_version}.so.tgz"

repository="DataDog/nginx-datadog"

if [ -n "$NGINX_DATADOG_VERSION" ]; then
  nginx_datadog_release_tag="v${NGINX_DATADOG_VERSION}"
else
  nginx_datadog_release_tag=$(get_latest_release "$repository")
fi

wget "https://github.com/$repository/releases/download/$nginx_datadog_release_tag/$tarball"
tar -xzf "$tarball" -C "$nginx_modules_path"
rm "$tarball"
