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
install build-essential libtool autoconf unzip wget tar curl git ca-certificates

# nproc, etc.
install coreutils

# nginx uses perl-compatible regular expressions (PCRE) and zlib (for gzip).
install libpcre3-dev zlib1g-dev

# Older debians will have out of date certificate authorities, which will cause
# wget to fail.  This heavy-handed update prevents that.
apt-get update
DEBIAN_FRONTENT=noninteractive apt-get upgrade --yes

# Install a recent cmake.  We need at least 3.24 (for dd-trace-cpp),
# but package managers tend to have an earlier version.
# Kitware releases an installer for glibc-based Linuxen.
CMAKE_VERSION=3.26.1
ARCHITECTURE=$(uname -m)
CMAKE_INSTALLER=cmake-${CMAKE_VERSION}-linux-${ARCHITECTURE}.sh
URL=https://github.com/Kitware/CMake/releases/download/v${CMAKE_VERSION}/${CMAKE_INSTALLER}

cd /tmp
if ! wget "${URL}"; then
    >&2 echo "wget failed to download \"${URL}\"."
    exit 1
fi
chmod +x "${CMAKE_INSTALLER}"
./"${CMAKE_INSTALLER}" --skip-license --prefix=/usr/local --exclude-subdir
rm "${CMAKE_INSTALLER}"
