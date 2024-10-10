#!/usr/bin/env bash
# Script executed by the 1-pipeline (internal tool) to prepare
# OCI artifacts that will be used by the Datadog Installer.
#
# Limitations: One OCI per nginx-datadog module. The OCI will contains
# all supported NGINX versions.
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

python3 ../bin/prepare_oci_package.py --version-tag v1.3.1 --arch ${arch}
