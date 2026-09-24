#!/bin/bash
# Run the shared clang-tidy baseline on first-party C++ sources.
# Findings fail the check.
#
# Locally this script re-execs in Docker (needs NGINX_VERSION).
# In CI / already in the container it configures, builds the module so
# generated nginx headers exist, then runs clang-tidy.

set -eo pipefail

alpine_version=3.23.4
container_image=${NGINX_TIDY_IMAGE:-alpine:$alpine_version}
container_repo=/repo
default_build_root=".clang-tidy-build/alpine-$alpine_version"

if [ -z "$NGINX_VERSION" ]; then
    >&2 echo 'NGINX_VERSION is not set. Please set the NGINX_VERSION environment variable.'
    exit 1
fi

if [ "$NGINX_TIDY_IN_CONTAINER" != "1" ]; then
    repo_root=$(git rev-parse --show-toplevel)

    if ! command -v docker >/dev/null 2>&1; then
        >&2 echo 'docker is required to run clang-tidy.'
        exit 1
    fi

    exec docker run --rm -t \
        -e NGINX_TIDY_IN_CONTAINER=1 \
        -e BUILD_DIR \
        -e BUILD_TYPE \
        -e MAKE_JOB_COUNT \
        -e NGINX_VERSION \
        -e RUM \
        -e WAF \
        -v "$repo_root:$container_repo" \
        -w "$container_repo" \
        "$container_image" \
        sh -c 'apk add --no-cache bash >/dev/null && exec bash "$@"' \
        _ "$container_repo/bin/lint-tidy.sh" "$@"
fi

apk add --no-cache \
    ca-certificates \
    clang \
    clang-extra-tools \
    cmake \
    git \
    ninja \
    pcre2-dev \
    python3 \
    zlib-dev

jobs=${MAKE_JOB_COUNT:-}
if [ -z "$jobs" ]; then
    jobs=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)
fi

build_dir=${BUILD_DIR:-$default_build_root/project}
nginx_version=$NGINX_VERSION
build_type=${BUILD_TYPE:-Debug}
waf=${WAF:-ON}
rum=${RUM:-OFF}

export CC=clang
export CXX=clang++

case "$build_dir" in
    /*) ;;
    *) build_dir="$container_repo/$build_dir" ;;
esac

cmake -S "$container_repo" -B "$build_dir" -G Ninja \
    -DCMAKE_C_COMPILER=clang \
    -DCMAKE_CXX_COMPILER=clang++ \
    -DNGINX_VERSION="$nginx_version" \
    -DBUILD_TESTING=OFF \
    -DCMAKE_BUILD_TYPE="$build_type" \
    -DNGINX_DATADOG_ASM_ENABLED="$waf" \
    -DNGINX_DATADOG_RUM_ENABLED="$rum"

cmake --build "$build_dir" --target nginx_module -j "$jobs"

if ! command -v run-clang-tidy >/dev/null 2>&1 && \
   ! command -v clang-tidy >/dev/null 2>&1; then
    >&2 echo "clang-tidy is not installed."
    exit 1
fi

# Only analyze translation units in the compilation database. Passing every
# file under src/ and test/unit/ (RUM-off, tests-off) produces missing-header
# clang-diagnostic-error noise that is not a tidy finding.
common_args=(
    -p "$build_dir"
    -quiet
    "-header-filter=^$container_repo/src/.*"
    -system-headers=false
    -extra-arg=-Wno-error
    -extra-arg=-Wno-unknown-warning-option
    -extra-arg=-Wno-unused-command-line-argument
    -extra-arg=-Wno-everything
)

if [ "$#" -gt 0 ]; then
    clang-tidy "${common_args[@]}" --use-color "$@"
else
    run-clang-tidy "${common_args[@]}" -use-color -j "$jobs" \
        "-source-filter=^$container_repo/src/.*\\.(c|cpp)$"
fi
