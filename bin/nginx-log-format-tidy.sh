#!/bin/bash
set -eo pipefail

container_repo=/repo

if [ "${NGINX_LOG_FORMAT_TIDY_IN_CONTAINER:-}" != "1" ]; then
    repo_root=$(git rev-parse --show-toplevel)

    if ! command -v docker >/dev/null 2>&1; then
        >&2 echo 'docker is required to run the nginx log format clang-tidy check.'
        exit 1
    fi

    exec docker run --rm -t \
        -e NGINX_LOG_FORMAT_TIDY_IN_CONTAINER=1 \
        -e BUILD_TYPE="${BUILD_TYPE:-}" \
        -e BUILD_DIR="${BUILD_DIR:-}" \
        -e MAKE_JOB_COUNT="${MAKE_JOB_COUNT:-}" \
        -e NGINX_VERSION="${NGINX_VERSION:-}" \
        -e NGINX_LOG_FORMAT_TIDY_BUILD_DIR="${NGINX_LOG_FORMAT_TIDY_BUILD_DIR:-}" \
        -e WAF="${WAF:-}" \
        -e RUM="${RUM:-}" \
        -e NGINX_CONF_ARGS="${NGINX_CONF_ARGS:-}" \
        -v "$repo_root:$container_repo" \
        -w "$container_repo" \
        "${NGINX_LOG_FORMAT_TIDY_IMAGE:-alpine:3.23.4}" \
        sh -c 'apk add --no-cache bash >/dev/null && exec bash "$@"' \
        _ "$container_repo/bin/nginx-log-format-tidy.sh" "$@"
fi

apk add --no-cache \
    ca-certificates \
    clang \
    clang-dev \
    clang-extra-tools \
    clang21-static \
    cmake \
    git \
    llvm-dev \
    llvm-gtest \
    llvm-static \
    make \
    ninja \
    pcre2-dev \
    perl \
    python3 \
    zlib-dev

jobs=${MAKE_JOB_COUNT:-}
if [ -z "$jobs" ]; then
    jobs=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)
fi

build_dir=${BUILD_DIR:-.clang-tidy-build/alpine-3.23.4/project}
plugin_build_dir=${NGINX_LOG_FORMAT_TIDY_BUILD_DIR:-.clang-tidy-build/alpine-3.23.4/plugin}
nginx_version=${NGINX_VERSION:-1.31.1}
build_type=${BUILD_TYPE:-Debug}
waf=${WAF:-ON}
rum=${RUM:-OFF}

export CC=clang
export CXX=clang++

case "$build_dir" in
    /*) ;;
    *) build_dir="$container_repo/$build_dir" ;;
esac

case "$plugin_build_dir" in
    /*) ;;
    *) plugin_build_dir="$container_repo/$plugin_build_dir" ;;
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

llvm_config=$(command -v llvm-config 2>/dev/null || true)
if [ -z "$llvm_config" ]; then
    clang_major=$(clang --version | sed -E 's/.*version ([0-9]+).*/\1/; q')
    llvm_config=$(command -v "llvm-config-${clang_major}")
fi
llvm_cmake_dir=$("$llvm_config" --cmakedir)
cmake -S "$container_repo/tools/clang-tidy" -B "$plugin_build_dir" -G Ninja \
    -DCMAKE_C_COMPILER=clang \
    -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    "-DLLVM_DIR=$llvm_cmake_dir" \
    "-DClang_DIR=$(dirname "$llvm_cmake_dir")/clang"

cmake --build "$plugin_build_dir" --target NginxDatadogClangTidy -j "$jobs"

plugin="$plugin_build_dir/libNginxDatadogClangTidy.so"
if ! [ -e "$plugin" ]; then
    >&2 echo "Could not find built clang-tidy plugin at $plugin"
    exit 1
fi

common_args=(
    -p "$build_dir"
    "--load=$plugin"
    '-checks=-*,nginx-datadog-ngx-log-format'
    '-warnings-as-errors=nginx-datadog-ngx-log-format'
    "-header-filter=^$container_repo/src/.*"
    --quiet
    --system-headers=false
    --extra-arg=-DNGX_DEBUG=1
    --extra-arg=-Wno-error
    --extra-arg=-Wno-everything
    --extra-arg=-Wno-unknown-warning-option
    --extra-arg=-Wno-unused-command-line-argument
)

if [ "$#" -gt 0 ]; then
    clang-tidy "${common_args[@]}" "$@"
else
    python3 - "$container_repo" "$build_dir/compile_commands.json" <<'PY' \
        | xargs -0 clang-tidy "${common_args[@]}"
import json
import pathlib
import sys

repo_root = pathlib.Path(sys.argv[1]).resolve()
compile_commands = pathlib.Path(sys.argv[2])
src_root = repo_root / "src"
seen = set()

with compile_commands.open(encoding="utf-8") as handle:
    commands = json.load(handle)

for entry in commands:
    path = pathlib.Path(entry["file"])
    if not path.is_absolute():
        path = pathlib.Path(entry["directory"]) / path
    path = path.resolve()

    try:
        path.relative_to(src_root)
    except ValueError:
        continue

    if path.suffix not in {".c", ".cpp"} or path in seen:
        continue

    seen.add(path)
    sys.stdout.buffer.write(str(path).encode())
    sys.stdout.buffer.write(b"\0")
PY
fi
