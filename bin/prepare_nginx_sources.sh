#!/usr/bin/env bash
# bin/prepare_nginx_sources.sh
#
# Downloads and configures nginx sources for one or more versions.
# After running, each version has headers + generated objs/ files
# ready for the module build (Stage 2).
#
# Usage:
#   prepare_nginx_sources.sh <output_dir> <version> [<version> ...]
#
# Options (via environment):
#   WAF=ON          Include --with-threads (needed for ASM/WAF builds)
#   PARALLEL_JOBS   Max parallel configure jobs (default: nproc)
#   MODULE_DIR      Path to the nginx dynamic module directory
#                   (default: <script_dir>/../module)

set -euo pipefail

if [[ $# -lt 2 ]]; then
  echo "Usage: $0 <output_dir> <version> [<version> ...]" >&2
  exit 1
fi

OUTPUT_DIR="$1"
shift
VERSIONS=("$@")

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
MODULE_DIR="${MODULE_DIR:-${REPO_ROOT}/module}"
PARALLEL_JOBS="${PARALLEL_JOBS:-$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)}"

WAF="${WAF:-OFF}"

mkdir -p "${OUTPUT_DIR}"

configure_one_version() {
  local version="$1"
  local version_dir="${OUTPUT_DIR}/${version}"

  # Skip if already configured (idempotent)
  if [[ -f "${version_dir}/objs/ngx_http_datadog_module_modules.c" ]]; then
    echo "[skip] nginx ${version} already configured in ${version_dir}"
    return 0
  fi

  local tarball_url="https://nginx.org/download/nginx-${version}.tar.gz"
  local tarball="${OUTPUT_DIR}/nginx-${version}.tar.gz"

  # Download tarball if not cached
  if [[ ! -f "${tarball}" ]]; then
    echo "[download] nginx ${version}"
    curl -sSfL -o "${tarball}" "${tarball_url}"
  fi

  # Extract to a temp directory, then move into place
  local tmp_dir="${OUTPUT_DIR}/.tmp-${version}"
  rm -rf "${tmp_dir}"
  mkdir -p "${tmp_dir}"
  tar -xzf "${tarball}" -C "${tmp_dir}" --strip-components=1

  # Build configure arguments
  local conf_args=(
    "--add-dynamic-module=${MODULE_DIR}"
    "--with-compat"
  )

  if [[ "${WAF}" == "ON" ]]; then
    conf_args+=("--with-threads")
  fi

  # Run nginx's configure script
  echo "[configure] nginx ${version}"
  (cd "${tmp_dir}" && ./configure "${conf_args[@]}" > /dev/null 2>&1)

  # Move into final location atomically
  rm -rf "${version_dir}"
  mv "${tmp_dir}" "${version_dir}"

  echo "[done] nginx ${version} -> ${version_dir}"
}

export -f configure_one_version
export OUTPUT_DIR MODULE_DIR WAF

echo "Preparing nginx sources for ${#VERSIONS[@]} version(s) (parallel=${PARALLEL_JOBS})"

# Run configure for each version, parallelized
printf '%s\n' "${VERSIONS[@]}" | \
  xargs -P "${PARALLEL_JOBS}" -I{} bash -c 'configure_one_version "$@"' _ {}

echo "All nginx sources prepared in ${OUTPUT_DIR}"
