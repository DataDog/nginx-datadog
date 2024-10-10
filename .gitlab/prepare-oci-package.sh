#!/usr/bin/env bash
# Script executed by the 1-pipeline (internal tool) to prepare
# OCI artifacts that will be used by the Datadog Installer.
set -euo pipefail

arch=${ARCH:-$(uname -m)}

case "$(uname -m)" in
  aarch64|arm64)
    echo "arm64"
    ;;
  x86_64|amd64)
    echo "amd64"
    ;;
  *)
    exit 1
    ;;
esac

NGINX_VERSION="1.26.0"
TARBALL="ngx_http_datadog_module-${arch}-${NGINX_VERSION}.so.tgz"

curl -Lo ${TARBALL} "https://github.com/DataDog/nginx-datadog/releases/download/v1.3.1/${TARBALL}"
tar -xzf "$TARBALL"
rm "$TARBALL"
