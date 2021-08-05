#!/bin/sh

# Exit if a non-conditional command returns a nonzero exit status.
set -e

apt-get update

install() {
    # On newer ubuntus, some packages have interactive installers (e.g. "pick
    # your time zone").
    # Setting DEBIAN_FRONTEND=noninteractive prevents a stalled installer.
    DEBIAN_FRONTEND=noninteractive apt-get install --yes --quiet "$@"
}

# for downloading releases of build dependencies, unpacking them, and building them
install build-essential libtool autoconf unzip wget tar

# nproc, etc.
install coreutils

# nginx uses perl-compatible regular expressions (PCRE)
install libpcre3-dev

# Build a recent cmake from source.  We need at least 3.12, but ubuntu 18
# packages 3.10.
# Version 3.12 or newer is needed because version 3.12 added the ability to
# associated link libraries with OBJECT targets.  OBJECT targets don't link
# anything (they just produce .o files), but the idea is that link dependencies
# are inherited by targets that eventually will link the produced object files.
install libssl-dev # cmake likes to have openssl sources available
cmake_version=3.21.1
mkdir -p /tmp/build-cmake
cd /tmp/build-cmake
wget https://github.com/Kitware/CMake/releases/download/v$cmake_version/cmake-$cmake_version.tar.gz
tar -xzvf cmake-$cmake_version.tar.gz
cd cmake-$cmake_version
./bootstrap
make -j "$(nproc)"
make install
