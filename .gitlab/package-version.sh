#!/bin/bash
# Prints the OCI package version. Same file in httpd-datadog, nginx-datadog
# and inject-browser-sdk.
#   Release tag <prefix>X.Y.Z: X.Y.Z.
#   Otherwise: <last release>-dev.b<branch hash>.glci<pipeline>.g<sha>.
# PACKAGE_TAG_PREFIX: release tag prefix (default "v").
# PACKAGE_PIPELINE_ID: pipeline id (default CI_PIPELINE_ID). A child pipeline
# passes its parent's id. Use a pipeline id, not a job id: all arches must get
# one version.
set -euo pipefail

prefix="${PACKAGE_TAG_PREFIX:-v}"
release_re='^[0-9]+\.[0-9]+\.[0-9]+$'

tag="${CI_COMMIT_TAG:-}"
if [[ $tag == "$prefix"* && ${tag#"$prefix"} =~ $release_re ]]; then
  echo "${tag#"$prefix"}"
  exit 0
fi

pipeline="${PACKAGE_PIPELINE_ID:-${CI_PIPELINE_ID:?}}"

# The CI clone is shallow and lacks most tags, so ask origin.
last_release="$(git ls-remote --tags --refs origin "${prefix}*" |
  sed "s#.*refs/tags/${prefix}##" | grep -E "$release_re" | sort -V | tail -1 || true)"
if [ -z "$last_release" ]; then
  echo "ERROR: no ${prefix}X.Y.Z tag on origin (or git ls-remote failed, see above)" >&2
  exit 1
fi

branch_hash="$(printf '%s' "$CI_COMMIT_REF_NAME" | sha256sum | cut -c1-6)"
echo "${last_release}-dev.b${branch_hash}.glci${pipeline}.g${CI_COMMIT_SHA:0:8}"
