#!/bin/sh

# Exit if a non-conditional command returns a nonzero exit status.
set -e

yum update -y

# for downloading releases of build tools, and SSL for CMake build
# nginx uses perl-compatible regular expressions (PCRE) and zlib (for gzip).
yum install -y wget openssl openssl-devel pcre-devel zlib-devel clang

# C and C++ build toolchains, and a bunch of other utilities like git.
yum groupinstall -y 'Development Tools'

# Install a more recent version of GCC, and set up symbolic links so that CMake
# will choose the more recent installation (/usr/local/bin comes before /usr/bin
# in PATH).
#
# Amazon Linux 2, as of this writing, packages gcc 7.x by default, which has
# incomplete support for C++17, which dd-trace-cpp requires.
#
# gcc 10 does better.
#
# If Amazon Linux 2023, ingore
. /etc/os-release
if [[ $VERSION_ID = "2" ]]
then
  yum install -y gcc10 gcc10-c++
  ln -s /usr/bin/gcc10-gcc /usr/local/bin/cc
  ln -s /usr/bin/gcc10-g++ /usr/local/bin/c++
elif [[ $VERSION_ID = "2023" ]]
then
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
