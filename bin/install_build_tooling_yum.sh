#!/bin/sh

# Exit if a non-conditional command returns a nonzero exit status.
set -e

yum update -y

# for downloading releases of build tools, and SSL for CMake build
# nginx uses perl-compatible regular expressions (PCRE) and zlib (for gzip).
yum install -y wget openssl openssl-devel pcre-devel zlib-devel

# C and C++ build toolchains, and a bunch of other utilities like git.
yum groupinstall -y 'Development Tools'

# Build a recent cmake from source.  We need at least 3.12, but ubuntu 18
# packages 3.10.
# Version 3.12 or newer is needed because version 3.12 added the ability to
# associated link libraries with OBJECT targets.  OBJECT targets don't link
# anything (they just produce .o files), but the idea is that link dependencies
# are inherited by targets that eventually will link the produced object files.

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
