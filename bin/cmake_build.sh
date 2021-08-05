#!/bin/sh

# Exit if any non-conditional command returns a nonzero exit status.
set -e

build_dir=${1:-.build}

mkdir -p "$build_dir"
cd "$build_dir"
cmake -DBUILD_TESTING=OFF ..
make -j "$(nproc)"
