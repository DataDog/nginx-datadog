#!/usr/bin/env bash
# Verify that the nginx versions in .gitlab/build-and-test-all.yml match
# the canonical list in nginx_versions.txt.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VERSIONS_FILE="$REPO_ROOT/nginx_versions.txt"
CI_FILE="$REPO_ROOT/.gitlab/build-and-test-all.yml"

if [[ ! -f "$VERSIONS_FILE" ]]; then
  echo "ERROR: $VERSIONS_FILE not found" >&2
  exit 1
fi
if [[ ! -f "$CI_FILE" ]]; then
  echo "ERROR: $CI_FILE not found" >&2
  exit 1
fi

# Extract versions from nginx_versions.txt (skip comments and blank lines).
canonical=$(grep -v '^\s*#' "$VERSIONS_FILE" | grep -v '^\s*$' | sort)

# Extract NGINX_VERSION values from the build-nginx-all job in the CI YAML.
# We scope to the build-nginx-all section (up to the next top-level key) so
# we don't accidentally pick up INGRESS_NGINX_VERSION or RESTY_VERSION entries.
ci_versions=$(awk '
  /^build-nginx-all:/ { capture=1 }
  capture && /^[a-zA-Z]/ && !/^build-nginx-all:/ { capture=0 }
  capture { print }
' "$CI_FILE" \
  | grep -oE '"[0-9]+\.[0-9]+\.[0-9]+"' \
  | tr -d '"' \
  | sort -u)

if [[ "$canonical" == "$ci_versions" ]]; then
  echo "OK: nginx_versions.txt and $CI_FILE are in sync ($(echo "$canonical" | wc -l | tr -d ' ') versions)."
  exit 0
else
  echo "MISMATCH between nginx_versions.txt and $CI_FILE" >&2
  echo "" >&2
  echo "In nginx_versions.txt but NOT in CI YAML:" >&2
  comm -23 <(echo "$canonical") <(echo "$ci_versions") >&2
  echo "" >&2
  echo "In CI YAML but NOT in nginx_versions.txt:" >&2
  comm -13 <(echo "$canonical") <(echo "$ci_versions") >&2
  exit 1
fi
