#!/bin/sh

# Exit if a non-conditional command returns a nonzero exit status.
set -e

apk update

# for downloading releases of build dependencies, unpacking them, and building them
apk add build-base libtool autoconf wget tar curl git openssh

# nproc, etc.
apk add coreutils

# nginx uses perl-compatible regular expressions (PCRE) and zlib (for gzip).
apk add pcre-dev zlib-dev

# Build a recent cmake from source.  We need at least 3.12, but ubuntu 18
# packages 3.10.
# Version 3.12 or newer is needed because version 3.12 added the ability to
# associated link libraries with OBJECT targets.  OBJECT targets don't link
# anything (they just produce .o files), but the idea is that link dependencies
# are inherited by targets that eventually will link the produced object files.

# cmake likes to have openssl and Linux headers available
apk add openssl-dev linux-headers

cmake_version=3.21.1
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
