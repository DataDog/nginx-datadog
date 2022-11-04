#!/bin/sh

# RESTY_VERSION="1.21.4.1"
RESTY_OPENSSL_VERSION="1.1.1q"
RESTY_OPENSSL_PATCH_VERSION="1.1.1f"
RESTY_OPENSSL_URL_BASE="https://www.openssl.org/source"
RESTY_PCRE_VERSION="8.45"
RESTY_PCRE_BUILD_OPTIONS="--enable-jit"
RESTY_PCRE_SHA256="4e6ce03e0336e8b4a3d6c2b70b1c5e18590a5673a98186da90d4f33c23defc09"
last_cpu=$(grep '^processor\s*:' /proc/cpuinfo | tail -1 | awk '{print $3}')
cpu_count=$((last_cpu + 1)) # zero-based
RESTY_J="$cpu_count"

# Install build dependencies in a special (temporary) location.
apk add --no-cache --virtual .build-deps \
    build-base \
    coreutils \
    curl \
    gd-dev \
    geoip-dev \
    libxslt-dev \
    linux-headers \
    make \
    perl-dev \
    readline-dev \
    zlib-dev

# Install runtime dependencies.
apk add --no-cache \
    gd \
    geoip \
    libgcc \
    libxslt \
    zlib

cd /tmp

# OpenSSL
cd /tmp
curl -fSL "${RESTY_OPENSSL_URL_BASE}/openssl-${RESTY_OPENSSL_VERSION}.tar.gz" -o openssl-${RESTY_OPENSSL_VERSION}.tar.gz
tar xzf openssl-${RESTY_OPENSSL_VERSION}.tar.gz
cd openssl-${RESTY_OPENSSL_VERSION}
if [ $(echo ${RESTY_OPENSSL_VERSION} | cut -c 1-5) = "1.1.1" ]; then
    echo 'patching OpenSSL 1.1.1 for OpenResty'
    curl -s https://raw.githubusercontent.com/openresty/openresty/master/patches/openssl-${RESTY_OPENSSL_PATCH_VERSION}-sess_set_get_cb_yield.patch | patch -p1
fi
if [ $(echo ${RESTY_OPENSSL_VERSION} | cut -c 1-5) = "1.1.0" ]; then
    echo 'patching OpenSSL 1.1.0 for OpenResty'
    curl -s https://raw.githubusercontent.com/openresty/openresty/ed328977028c3ec3033bc25873ee360056e247cd/patches/openssl-1.1.0j-parallel_build_fix.patch | patch -p1
    curl -s https://raw.githubusercontent.com/openresty/openresty/master/patches/openssl-${RESTY_OPENSSL_PATCH_VERSION}-sess_set_get_cb_yield.patch | patch -p1
fi
./config \
    no-threads shared zlib -g \
    enable-ssl3 enable-ssl3-method \
    --prefix=/usr/local/openresty/openssl \
    --libdir=lib \
    -Wl,-rpath,/usr/local/openresty/openssl/lib
make -j${RESTY_J}
make -j${RESTY_J} install_sw

# PCRE
cd /tmp
curl -fSL https://downloads.sourceforge.net/project/pcre/pcre/${RESTY_PCRE_VERSION}/pcre-${RESTY_PCRE_VERSION}.tar.gz -o pcre-${RESTY_PCRE_VERSION}.tar.gz
echo "${RESTY_PCRE_SHA256}  pcre-${RESTY_PCRE_VERSION}.tar.gz" | shasum -a 256 --check
tar xzf pcre-${RESTY_PCRE_VERSION}.tar.gz
cd /tmp/pcre-${RESTY_PCRE_VERSION}
./configure \
    --prefix=/usr/local/openresty/pcre \
    --disable-cpp \
    --enable-utf \
    --enable-unicode-properties \
    ${RESTY_PCRE_BUILD_OPTIONS}
make -j${RESTY_J}
make -j${RESTY_J} install
