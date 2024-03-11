#!/bin/sh

# Exit if a non-conditional command returns a nonzero exit status.
set -e

yum update -y

# for downloading releases of build tools, and SSL for CMake build
# nginx uses perl-compatible regular expressions (PCRE) and zlib (for gzip).
yum install -y wget openssl openssl-devel pcre-devel zlib-devel

# C and C++ build toolchains, and a bunch of other utilities like git.
yum groupinstall -y 'Development Tools'

compiler_major_version() {
    # `g++ --version` looks something like:
    #
    #     g++ (GCC) 11.4.1 20230605 (Red Hat 11.4.1-2)
    #     Copyright (C) 2021 Free Software Foundation, Inc.
    #     This is free software; see the source for copying conditions.  There is NO
    #     warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
    #
    # We're interested in the major version number. So,
    #
    # - take the first line
    # - take the third "word" ("11.4.1")
    # - take until before the first period ("11")
    # - the major version is 11.
    g++ --version | head -1 | awk '{print $3}' | sed 's/\..*//'
}

# If g++ is older than major version 10, then install a more recent version of
# GCC, and set up symbolic links so that CMake will choose the more recent
# installation (/usr/local/bin comes before /usr/bin in PATH).
#
# Older versions of Amazon Linux package gcc 7.x by default, which has
# incomplete support for C++17, which dd-trace-cpp requires.
if [ "$(compiler_major_version)" -lt 10 ]; then
    yum install -y gcc10 gcc10-c++
    ln -s /usr/bin/gcc10-gcc /usr/local/bin/cc
    ln -s /usr/bin/gcc10-g++ /usr/local/bin/c++
fi

# On newer versions of Amazon Linux, the static version of libstdc++ is a
# separate package. If present, the package will be called "libstdc++-static".
# Install it if it's available.
if 2>/dev/null 1>&2 yum list libstdc++-static; then
    yum install -y libstdc++-static
fi

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
