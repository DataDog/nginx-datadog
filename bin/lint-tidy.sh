#!/bin/bash
# Run the pinned clang-tidy through CMake in a container.
#
# clang-tidy must use the same compiler, flags, and sysroot as the real
# build. CMake's CXX_CLANG_TIDY on ngx_http_datadog_objs does that: it
# invokes tidy with the exact compile line after `--`. Do not use a host
# compilation database.
#
# Usage: NGINX_VERSION=<version> make lint-tidy

set -eo pipefail

# Bump these together. alpine:3.23's default clang is 21; install the
# versioned LLVM 19 packages so tidy stays on 19.
alpine_version=3.23.4
CLANG_TIDY_VERSION=19
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
    "clang${CLANG_TIDY_VERSION}" \
    "clang${CLANG_TIDY_VERSION}-extra-tools" \
    cmake \
    git \
    ninja \
    pcre2-dev \
    zlib-dev

first_cmd() {
    for candidate in "$@"; do
        if [ -x "$candidate" ]; then
            echo "$candidate"
            return 0
        fi
        if command -v "$candidate" >/dev/null 2>&1; then
            command -v "$candidate"
            return 0
        fi
    done
    return 1
}

llvm_bin="/usr/lib/llvm${CLANG_TIDY_VERSION}/bin"
c_compiler=$(first_cmd \
    "clang-${CLANG_TIDY_VERSION}" \
    "${llvm_bin}/clang")
compiler=$(first_cmd \
    "clang++-${CLANG_TIDY_VERSION}" \
    "${llvm_bin}/clang++")
tidy=$(first_cmd \
    "clang-tidy-${CLANG_TIDY_VERSION}" \
    "${llvm_bin}/clang-tidy")
if [ -z "$compiler" ] || [ -z "$tidy" ] || [ -z "$c_compiler" ]; then
    >&2 echo "clang-${CLANG_TIDY_VERSION} / clang-tidy-${CLANG_TIDY_VERSION} not found after apk add."
    exit 1
fi

export CC="$c_compiler"
export CXX="$compiler"

tidy_major=$("$tidy" --version | sed -n 's/.*version \([0-9][0-9]*\).*/\1/p' | head -n 1)
compiler_major=$("$compiler" -dumpversion | cut -d. -f1)
if [ "$compiler_major" != "$CLANG_TIDY_VERSION" ] || [ "$tidy_major" != "$CLANG_TIDY_VERSION" ]; then
    >&2 echo "clang-tidy and the CMake compiler must both be LLVM ${CLANG_TIDY_VERSION}."
    >&2 echo "  $compiler: ${compiler_major}"
    >&2 echo "  $tidy: ${tidy_major}"
    exit 1
fi

jobs=${MAKE_JOB_COUNT:-}
if [ -z "$jobs" ]; then
    jobs=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)
fi

build_dir=${BUILD_DIR:-$default_build_root/project}
nginx_version=$NGINX_VERSION
build_type=${BUILD_TYPE:-Debug}
waf=${WAF:-ON}
rum=${RUM:-OFF}

case "$build_dir" in
    /*) ;;
    *) build_dir="$container_repo/$build_dir" ;;
esac

# CXX_CLANG_TIDY is attached only to ngx_http_datadog_objs. Building
# nginx_module compiles first-party src/ with the exact compile line
# (RUM off by default, so src/rum/ is not a source of that target).
cmake -S "$container_repo" -B "$build_dir" -G Ninja \
    -DCMAKE_C_COMPILER="$c_compiler" \
    -DCMAKE_CXX_COMPILER="$compiler" \
    -DNGINX_VERSION="$nginx_version" \
    -DBUILD_TESTING=OFF \
    -DCMAKE_BUILD_TYPE="$build_type" \
    -DNGINX_DATADOG_ASM_ENABLED="$waf" \
    -DNGINX_DATADOG_RUM_ENABLED="$rum" \
    -DNGINX_DATADOG_ENABLE_CLANG_TIDY=ON \
    -DNGINX_DATADOG_CLANG_TIDY="$tidy"

cmake --build "$build_dir" --target nginx_module -j "$jobs"
