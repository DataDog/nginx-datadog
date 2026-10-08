#!/bin/bash
# Run clang-tidy in the CI container: stock checks, then the nginx
# log-format plugin.
#
# Configures CMake, builds nginx_module so generated headers exist, then
# run-clang-tidy -p on src/.
#
# Usage: NGINX_VERSION=<version> make lint

set -eo pipefail

# Bump these together. alpine:3.23's default clang is 21; install the
# versioned LLVM 19 packages so tidy stays on 19.
alpine_version=3.23.4
CLANG_TIDY_VERSION=19
container_image=${NGINX_LINT_IMAGE:-alpine:$alpine_version}
container_repo=/repo
default_build_root=".clang-tidy-build/alpine-$alpine_version"

if [ -z "$NGINX_VERSION" ]; then
    >&2 echo 'NGINX_VERSION is not set. Please set the NGINX_VERSION environment variable.'
    exit 1
fi

if [ "${NGINX_LINT_IN_CONTAINER:-}" != "1" ]; then
    repo_root=$(git rev-parse --show-toplevel)

    exec docker run --rm -t \
        -e NGINX_LINT_IN_CONTAINER=1 \
        -e BUILD_DIR \
        -e BUILD_TYPE \
        -e MAKE_JOB_COUNT \
        -e NGINX_CONF_ARGS \
        -e NGINX_LOG_FORMAT_TIDY_BUILD_DIR \
        -e NGINX_VERSION \
        -e RUM \
        -e WAF \
        -v "$repo_root:$container_repo" \
        -w "$container_repo" \
        "$container_image" \
        sh -c 'apk add --no-cache bash >/dev/null && exec bash "$@"' \
        _ "$container_repo/bin/lint.sh" "$@"
fi

apk add --no-cache \
    ca-certificates \
    "clang${CLANG_TIDY_VERSION}" \
    "clang${CLANG_TIDY_VERSION}-dev" \
    "clang${CLANG_TIDY_VERSION}-extra-tools" \
    "clang${CLANG_TIDY_VERSION}-static" \
    cmake \
    git \
    "llvm${CLANG_TIDY_VERSION}-dev" \
    "llvm${CLANG_TIDY_VERSION}-gtest" \
    "llvm${CLANG_TIDY_VERSION}-static" \
    make \
    ninja \
    pcre2-dev \
    perl \
    python3 \
    zlib-dev

# Alpine LLVM layout (no hyphen).
llvm_bin=/usr/lib/llvm${CLANG_TIDY_VERSION}/bin
clang_tidy=$llvm_bin/clang-tidy
run_clang_tidy=$llvm_bin/run-clang-tidy
if [ ! -x "$run_clang_tidy" ] && [ -x /usr/bin/run-clang-tidy ]; then
    # Alpine ships the runner unversioned; -clang-tidy-binary stays pinned.
    run_clang_tidy=/usr/bin/run-clang-tidy
fi

if [ ! -x "$clang_tidy" ] || [ ! -x "$run_clang_tidy" ]; then
    >&2 echo "$llvm_bin/clang-tidy is required (pinned)."
    exit 1
fi

jobs=${MAKE_JOB_COUNT:-}
if [ -z "$jobs" ]; then
    jobs=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)
fi

build_dir=${BUILD_DIR:-$default_build_root/project}
plugin_build_dir=${NGINX_LOG_FORMAT_TIDY_BUILD_DIR:-$default_build_root/plugin}
nginx_version=$NGINX_VERSION
build_type=${BUILD_TYPE:-Debug}
waf=${WAF:-ON}
rum=${RUM:-OFF}

case "$build_dir" in
    /*) ;;
    *) build_dir="$container_repo/$build_dir" ;;
esac

case "$plugin_build_dir" in
    /*) ;;
    *) plugin_build_dir="$container_repo/$plugin_build_dir" ;;
esac

# Generated nginx headers are required before tidy can parse src/.
CC=$llvm_bin/clang CXX=$llvm_bin/clang++ cmake -S "$container_repo" -B "$build_dir" \
    -G Ninja \
    --fresh \
    -DCMAKE_C_COMPILER="$llvm_bin/clang" \
    -DCMAKE_CXX_COMPILER="$llvm_bin/clang++" \
    -DNGINX_VERSION="$nginx_version" \
    -DBUILD_TESTING=OFF \
    -DCMAKE_BUILD_TYPE="$build_type" \
    -DNGINX_DATADOG_ASM_ENABLED="$waf" \
    -DNGINX_DATADOG_RUM_ENABLED="$rum"

cmake --build "$build_dir" --target nginx_module -j "$jobs"

python3 - "$build_dir/compile_commands.json" "$container_repo" "$llvm_bin" <<'PY'
import json, os, sys
db_path, root, llvm_bin = sys.argv[1], sys.argv[2], sys.argv[3]
verified_source_count = 0
for entry in json.load(open(db_path)):
    path = entry.get("file") or ""
    if not os.path.isabs(path):
        path = os.path.normpath(os.path.join(entry.get("directory", root), path))
    relative_path = os.path.relpath(path, root)
    if not relative_path.startswith("src/") or relative_path.startswith("src/rum/"):
        continue
    if not relative_path.endswith((".c", ".cc", ".cpp", ".cxx")):
        continue
    command = entry.get("command") or ""
    if llvm_bin not in command:
        sys.stderr.write(
            "BUILD_DIR was not configured with the pinned clang; refusing to run tidy.\n"
            "  missing " + llvm_bin + " in " + relative_path + "\n"
        )
        sys.exit(1)
    verified_source_count += 1
if verified_source_count == 0:
    sys.stderr.write("No src/ entries in " + db_path + ".\n")
    sys.exit(1)
PY

if ! echo '#include <string>' | "$llvm_bin/clang++" -x c++ - -fsyntax-only; then
    >&2 echo "C++ headers are not usable with $llvm_bin/clang++; refusing to run tidy."
    exit 1
fi

"$run_clang_tidy" -p "$build_dir" -clang-tidy-binary "$clang_tidy" \
    -header-filter "^$container_repo/src/" -quiet "^$container_repo/src/"

llvm_config=$llvm_bin/llvm-config
if [ ! -x "$llvm_config" ]; then
    llvm_config=$(command -v "llvm-config-${CLANG_TIDY_VERSION}")
fi
if [ -z "$llvm_config" ] || [ ! -x "$llvm_config" ]; then
    >&2 echo "llvm-config-${CLANG_TIDY_VERSION} is required to build the log-format plugin."
    exit 1
fi
llvm_cmake_dir=$("$llvm_config" --cmakedir)
cmake -S "$container_repo/tools/clang-tidy" -B "$plugin_build_dir" -G Ninja \
    -DCMAKE_C_COMPILER="$llvm_bin/clang" \
    -DCMAKE_CXX_COMPILER="$llvm_bin/clang++" \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    "-DLLVM_DIR=$llvm_cmake_dir" \
    "-DClang_DIR=$(dirname "$llvm_cmake_dir")/clang"

cmake --build "$plugin_build_dir" --target NginxDatadogClangTidy -j "$jobs"

plugin="$plugin_build_dir/libNginxDatadogClangTidy.so"
if ! [ -e "$plugin" ]; then
    >&2 echo "Could not find built clang-tidy plugin at $plugin"
    exit 1
fi

plugin_args=(
    -p "$build_dir"
    -clang-tidy-binary "$clang_tidy"
    "-load=$plugin"
    '-checks=-*,nginx-datadog-*'
    '-warnings-as-errors=nginx-datadog-*'
    "-header-filter=^$container_repo/src/.*"
    -quiet
    -extra-arg=-DNGX_DEBUG=1
    -extra-arg=-Wno-error
    -extra-arg=-Wno-everything
    -extra-arg=-Wno-unknown-warning-option
    -extra-arg=-Wno-unused-command-line-argument
)

if [ "$#" -gt 0 ]; then
    "$clang_tidy" "${plugin_args[@]}" --use-color -system-headers=false "$@"
else
    "$run_clang_tidy" "${plugin_args[@]}" -use-color -j "$jobs" \
        "-source-filter=^$container_repo/src/.*\\.(c|cpp)$" \
        | perl -pe 's#^(\[\s*\d+/\d+\]\[[^]]+\]) .*/repo/(src/\S+)$#$1 $2#'
fi
