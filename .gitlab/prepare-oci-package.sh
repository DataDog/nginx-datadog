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

rm -rf dd-library-php-ssi
tar xvzf ../dd-library-php-ssi-${arch}-linux.tar.gz
mv dd-library-php-ssi sources
