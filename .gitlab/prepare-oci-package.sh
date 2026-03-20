#!/bin/bash

# Called by one-pipeline's package-oci job to populate the sources/ directory
# that datadog-package create expects.
#
# Assumes current working directory is set by the package-oci job (a subdirectory of the repo root,
# so ../artifacts/ resolves to the ssi-build artifact directory).
#
# This script is used by the ssi-build and ssi-build-all jobs that are specific to RUM.
# Those CI jobs collect RUM-enabled nginx modules from
# build-nginx-rum-fast / build-nginx-rum-all and assemble them into
# artifacts/ssi-sources/<arch>/. This script copies those artifacts into
# sources/ for OCI packaging.

set -euo pipefail

if [ -z "${ARCH}" ]; then
  echo "ARCH not available as an environment variable"
  exit 1
fi

SOURCES_DIR="../artifacts/ssi-sources/${ARCH}"

if [ ! -d "${SOURCES_DIR}" ]; then
  echo "ERROR: Sources directory not found: ${SOURCES_DIR} (pwd: $(pwd))"
  echo "The ssi-build or ssi-build-all job must complete before package-oci runs."
  exit 1
fi

echo "Copying sources from ${SOURCES_DIR} to sources/"
mkdir -p sources
if [ -z "$(ls -A "${SOURCES_DIR}")" ]; then
  echo "ERROR: Sources directory is empty: ${SOURCES_DIR}"
  exit 1
fi
cp -r "${SOURCES_DIR}/"* sources/

so_count=$(find sources -name '*.so' -type f | wc -l)
if [ "$so_count" -eq 0 ]; then
  echo "ERROR: No .so module files found in sources/ after copy from ${SOURCES_DIR}"
  exit 1
fi

echo "Sources contents:"
find sources -type f | sort
