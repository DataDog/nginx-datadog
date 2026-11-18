#!/bin/bash
# Prints the OCI package version.
#   Tag vX.Y.Z: X.Y.Z.
#   Otherwise: <last release>-dev.b<branch hash>.glci<pipeline>.g<sha>.
# Use the pipeline id, not the job id: both arches must get one version.
set -euo pipefail

if [ -n "${CI_COMMIT_TAG:-}" ]; then
  echo "${CI_COMMIT_TAG#v}"
  exit 0
fi

# The CI clone is shallow and lacks most tags, so ask origin.
last_release="$(git ls-remote --tags --refs origin 'v*' |
  sed 's#.*refs/tags/v##' |
  grep -E '^[0-9]+\.[0-9]+\.[0-9]+$' |
  sort -V | tail -1 || true)"
if [ -z "$last_release" ]; then
  echo "ERROR: no vX.Y.Z tag on origin (or git ls-remote failed, see above)" >&2
  exit 1
fi

branch_hash="$(printf '%s' "$CI_COMMIT_REF_NAME" | sha256sum | cut -c1-6)"
echo "${last_release}-dev.b${branch_hash}.glci${CI_PIPELINE_ID}.g${CI_COMMIT_SHA:0:8}"
