#!/bin/sh
# Shared utility functions for install_datadog.sh scripts.

is_installed() {
  command -v "$1" > /dev/null 2>&1
}

get_latest_release() {
  tag=$(curl --silent --fail "https://api.github.com/repos/$1/releases/latest" | jq --raw-output .tag_name)
  if [ -z "$tag" ] || [ "$tag" = "null" ]; then
    >&2 echo "Failed to fetch latest release tag for $1. Check network and GitHub API rate limits."
    exit 1
  fi
  echo "$tag"
}
