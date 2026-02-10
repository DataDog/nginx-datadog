#!/bin/bash
# Generate SSI OCI package (dev build)
#
# This script builds a subset of nginx modules (1.28.1 and 1.29.4 only) with
# RUM injection and packages them into a multi-arch OCI package.
#
# Use this for quick testing. For release builds with all nginx versions,
# use generate-ssi-oci-package.sh instead.
#
# Usage:
#   ./bin/generate-ssi-oci-package-dev.sh [VERSION]
#   ./bin/generate-ssi-oci-package-dev.sh 1.11.0
#
# Output:
#   artifacts/ssi-packages/merged.tar - Multi-arch OCI package
#   artifacts/ssi-packages/datadog-nginx-ssi-<version>-linux-<arch>.tar - Per-arch packages

set -euo pipefail

VERSION="${1:-${SSI_PACKAGE_VERSION:-0.0.1-dev}}"

echo "Building SSI OCI package (dev) version ${VERSION}..."
echo "Nginx versions: 1.28.1, 1.29.4"

# Step 1: Build nginx modules and create per-arch packages
echo ""
echo "Step 1: Building modules and creating per-arch packages..."
SSI_PACKAGE_VERSION="$VERSION" docker buildx bake ssi-package-create-dev

# Step 2: Merge into multi-arch OCI index
echo ""
echo "Step 2: Merging into multi-arch OCI package..."
SSI_PACKAGE_VERSION="$VERSION" docker buildx bake ssi-package-merge-dev

echo ""
echo "========================================"
echo "Package created successfully!"
echo "========================================"
echo ""
echo "Output files:"
ls -la artifacts/ssi-packages/
echo ""
echo "To push to registry:"
echo "  datadog-packages/datadog-package push ghcr.io/datadog/datadog-nginx-ssi:${VERSION} artifacts/ssi-packages/merged.tar"
echo ""
echo "To inspect the package:"
echo "  tar -tvf artifacts/ssi-packages/merged.tar"
