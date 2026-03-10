#!/bin/bash

# Called by one-pipeline's package-oci job to populate the sources/ directory
# that datadog-package create expects.
#
# The ssi-build CI job exports assembled sources (nginx .so modules + version file)
# to artifacts/ssi-sources/<arch>/ via docker buildx bake ssi-package-assemble-<arch>.
# This script copies those artifacts into sources/ for OCI packaging.

set -eo pipefail

if [ -z "${ARCH}" ]; then
  echo "ARCH not available as an environment variable"
  exit 1
fi

SOURCES_DIR="../artifacts/ssi-sources/${ARCH}"

if [ ! -d "${SOURCES_DIR}" ]; then
  echo "ERROR: Sources directory not found: ${SOURCES_DIR}"
  echo "The ssi-build job must complete before package-oci runs."
  exit 1
fi

echo "Copying sources from ${SOURCES_DIR} to sources/"
mkdir -p sources
cp -r "${SOURCES_DIR}/"* sources/

echo "Sources contents:"
find sources -type f | sort
