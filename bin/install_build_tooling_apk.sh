#!/bin/sh

# Exit if a non-conditional command returns a nonzero exit status.
set -e

apk update

# for downloading releases of build dependencies, unpacking them, and building them
apk add build-base libtool autoconf wget tar curl git openssh bash

# nproc, etc.
apk add coreutils

# nginx uses perl-compatible regular expressions (PCRE) and zlib (for gzip).
apk add pcre-dev zlib-dev

# Build a recent cmake from source.  dd-trace-cpp requires a version more recent than
# what is commonly packaged.
# We have to build it from source, because Kitware doesn't produce binary
# releases on musl libc.

# cmake likes to have openssl and Linux headers available
apk add openssl-dev linux-headers

cmake_version=3.26.1
starting_dir=$(pwd)
mkdir -p /tmp/build-cmake
cd /tmp/build-cmake
wget https://github.com/Kitware/CMake/releases/download/v$cmake_version/cmake-$cmake_version.tar.gz
tar -xzvf cmake-$cmake_version.tar.gz
cd cmake-$cmake_version
./bootstrap
make --jobs="${MAKE_JOB_COUNT:-$(nproc)}"
make install
cd "$starting_dir"
rm -rf /tmp/build-cmake
