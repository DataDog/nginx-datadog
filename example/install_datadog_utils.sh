#!/bin/sh
# Shared utility functions for install_datadog.sh scripts.

detect_arch() {
  arch="$(uname -m)"
  case "$arch" in
      aarch64)
        arch="arm64"
        ;;
      x86_64)
        arch="amd64"
        ;;
      *)
        >&2 echo "Platform ${arch} is not supported."
        exit 1
        ;;
  esac
}

get_latest_release() {
  tag=$(curl --silent --fail "https://api.github.com/repos/$1/releases/latest" | jq --raw-output .tag_name)
  if [ -z "$tag" ] || [ "$tag" = "null" ]; then
    >&2 echo "Failed to fetch latest release tag for $1. Check network and GitHub API rate limits."
    exit 1
  fi
  echo "$tag"
}

install_packages() {
  # Install the requested list of packages.
  # `apt-get` (Debian, Ubuntu), `apk` (Alpine), and `yum` (Amazon Linux) are supported.
  if is_installed apt-get; then
      apt-get update
      DEBIAN_FRONTEND=noninteractive apt-get install -y "$@"
  elif is_installed apk; then
      apk update
      apk add "$@"
  elif is_installed yum; then
      yum update -y
      yum install -y "$@"
  else
      >&2 printf 'Did not find a supported package manager.\n'
      exit 3
  fi
}

is_installed() {
  command -v "$1" > /dev/null 2>&1
}
