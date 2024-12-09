#!/bin/sh
set -e

export RESTY_VERSION="1.27.1.1"
export NGINX_VERSION="1.27.1"

# https://github.com/openresty/openresty-packaging/blob/master/alpine/openresty-openssl3/APKBUILD
export RESTY_OPENSSL_VERSION="3.0.15"
export RESTY_OPENSSL_PATCH_VERSION="3.0.15"
export RESTY_OPENSSL_URL_BASE="https://github.com/openssl/openssl/releases/download/openssl-${RESTY_OPENSSL_VERSION}"
# LEGACY:  "https://www.openssl.org/source/old/1.1.1"
export RESTY_OPENSSL_BUILD_OPTIONS="enable-camellia enable-seed enable-rfc3779 enable-cms enable-md2 enable-rc5 \
        enable-weak-ssl-ciphers enable-ssl3 enable-ssl3-method enable-md2 enable-ktls enable-fips \
        "

# https://github.com/openresty/openresty-packaging/blob/master/alpine/openresty-pcre2/APKBUILD
export RESTY_PCRE_VERSION="10.44"
export RESTY_PCRE_SHA256="86b9cb0aa3bcb7994faa88018292bc704cdbb708e785f7c74352ff6ea7d3175b"
export RESTY_PCRE_BUILD_OPTIONS="--enable-jit --enable-pcre2grep-jit --disable-bsr-anycrlf --disable-coverage --disable-ebcdic --disable-fuzz-support \
    --disable-jit-sealloc --disable-never-backslash-C --enable-newline-is-lf --enable-pcre2-8 --enable-pcre2-16 --enable-pcre2-32 \
    --enable-pcre2grep-callout --enable-pcre2grep-callout-fork --disable-pcre2grep-libbz2 --disable-pcre2grep-libz --disable-pcre2test-libedit \
    --enable-percent-zt --disable-rebuild-chartables --enable-shared --disable-static --disable-silent-rules --enable-unicode --disable-valgrind \
    "
export RESTY_J="8"

# https://github.com/openresty/openresty-packaging/blob/master/alpine/openresty/APKBUILD
export RESTY_CONFIG_OPTIONS="\
    --with-compat \
    --without-http_rds_json_module \
    --without-http_rds_csv_module \
    --without-lua_rds_parser \
    --without-mail_pop3_module \
    --without-mail_imap_module \
    --without-mail_smtp_module \
    --with-http_addition_module \
    --with-http_auth_request_module \
    --with-http_dav_module \
    --with-http_flv_module \
    --with-http_geoip_module=dynamic \
    --with-http_gunzip_module \
    --with-http_gzip_static_module \
    --with-http_image_filter_module=dynamic \
    --with-http_mp4_module \
    --with-http_random_index_module \
    --with-http_realip_module \
    --with-http_secure_link_module \
    --with-http_slice_module \
    --with-http_ssl_module \
    --with-http_stub_status_module \
    --with-http_sub_module \
    --with-http_v2_module \
    --with-http_v3_module \
    --with-http_xslt_module=dynamic \
    --with-ipv6 \
    --with-mail \
    --with-mail_ssl_module \
    --with-md5-asm \
    --with-sha1-asm \
    --with-stream \
    --with-stream_ssl_module \
    --with-stream_ssl_preread_module \
    --with-threads \
    "
export RESTY_CONFIG_OPTIONS_MORE=""
export RESTY_LUAJIT_OPTIONS="--with-luajit-xcflags='-DLUAJIT_NUMMODE=2 -DLUAJIT_ENABLE_LUA52COMPAT'"
export RESTY_PCRE_OPTIONS="--with-pcre-jit"

export RESTY_EVAL_PRE_CONFIGURE=""
export RESTY_EVAL_POST_DOWNLOAD_PRE_CONFIGURE=""
export RESTY_EVAL_POST_MAKE=""

# These are not intended to be user-specified
export _RESTY_CONFIG_DEPS="--with-pcre \
    --with-cc-opt='-DNGX_LUA_ABORT_AT_PANIC -I/usr/local/openresty/pcre2/include -I/usr/local/openresty/openssl3/include' \
    --with-ld-opt='-L/usr/local/openresty/pcre2/lib -L/usr/local/openresty/openssl3/lib -Wl,-rpath,/usr/local/openresty/pcre2/lib:/usr/local/openresty/openssl3/lib' \
    "

cd /tmp
if [ -n "${RESTY_EVAL_PRE_CONFIGURE}" ]; then eval $(echo ${RESTY_EVAL_PRE_CONFIGURE}); fi
curl -fSL "${RESTY_OPENSSL_URL_BASE}/openssl-${RESTY_OPENSSL_VERSION}.tar.gz" -o openssl-${RESTY_OPENSSL_VERSION}.tar.gz
tar xzf openssl-${RESTY_OPENSSL_VERSION}.tar.gz
cd openssl-${RESTY_OPENSSL_VERSION}
if [ $(echo ${RESTY_OPENSSL_VERSION} | cut -c 1-5) = "3.0.15" ] ; then
    echo 'patching OpenSSL 3.0.15 for OpenResty'
    curl -s https://raw.githubusercontent.com/openresty/openresty/master/patches/openssl-${RESTY_OPENSSL_PATCH_VERSION}-sess_set_get_cb_yield.patch | patch -p1 ;
fi
if [ $(echo ${RESTY_OPENSSL_VERSION} | cut -c 1-5) = "1.1.1" ] ; then
    echo 'patching OpenSSL 1.1.1 for OpenResty'
    curl -s https://raw.githubusercontent.com/openresty/openresty/master/patches/openssl-${RESTY_OPENSSL_PATCH_VERSION}-sess_set_get_cb_yield.patch | patch -p1 ;
fi
if [ $(echo ${RESTY_OPENSSL_VERSION} | cut -c 1-5) = "1.1.0" ] ; then
    echo 'patching OpenSSL 1.1.0 for OpenResty'
    curl -s https://raw.githubusercontent.com/openresty/openresty/ed328977028c3ec3033bc25873ee360056e247cd/patches/openssl-1.1.0j-parallel_build_fix.patch | patch -p1
    curl -s https://raw.githubusercontent.com/openresty/openresty/master/patches/openssl-${RESTY_OPENSSL_PATCH_VERSION}-sess_set_get_cb_yield.patch | patch -p1 ;
fi
CC=clang CXX=clang++ ./config \
      shared zlib -g \
      --prefix=/usr/local/openresty/openssl3 \
      --libdir=lib \
      -Wl,-rpath,/usr/local/openresty/openssl3/lib \
      ${RESTY_OPENSSL_BUILD_OPTIONS}
make -j${RESTY_J}
make -j${RESTY_J} install_sw
cd /tmp
curl -fSL "https://github.com/PCRE2Project/pcre2/releases/download/pcre2-${RESTY_PCRE_VERSION}/pcre2-${RESTY_PCRE_VERSION}.tar.gz" -o pcre2-${RESTY_PCRE_VERSION}.tar.gz
echo "${RESTY_PCRE_SHA256}  pcre2-${RESTY_PCRE_VERSION}.tar.gz" | shasum -a 256 --check
tar xzf pcre2-${RESTY_PCRE_VERSION}.tar.gz
cd /tmp/pcre2-${RESTY_PCRE_VERSION}
CC=clang CXX=clang++ CFLAGS="-g -O3" ./configure \
        --prefix=/usr/local/openresty/pcre2 \
        --libdir=/usr/local/openresty/pcre2/lib \
        ${RESTY_PCRE_BUILD_OPTIONS}
CFLAGS="-g -O3" make -j${RESTY_J}
CFLAGS="-g -O3" make -j${RESTY_J} install
cd /tmp
curl -fSL https://openresty.org/download/openresty-${RESTY_VERSION}.tar.gz -o openresty-${RESTY_VERSION}.tar.gz
tar xzf openresty-${RESTY_VERSION}.tar.gz
cd /tmp/openresty-${RESTY_VERSION}
if [ -n "${RESTY_EVAL_POST_DOWNLOAD_PRE_CONFIGURE}" ]; then eval $(echo ${RESTY_EVAL_POST_DOWNLOAD_PRE_CONFIGURE}); fi
eval CC=clang CXX=clang++ ./configure -j${RESTY_J} ${_RESTY_CONFIG_DEPS} ${RESTY_CONFIG_OPTIONS} ${RESTY_CONFIG_OPTIONS_MORE} ${RESTY_LUAJIT_OPTIONS} ${RESTY_PCRE_OPTIONS}
make -j${RESTY_J}
make -j${RESTY_J} install
cd /tmp
if [ -n "${RESTY_EVAL_POST_MAKE}" ]; then eval $(echo ${RESTY_EVAL_POST_MAKE}); fi
rm -rf openssl-${RESTY_OPENSSL_VERSION}.tar.gz openssl-${RESTY_OPENSSL_VERSION} \
pcre2-${RESTY_PCRE_VERSION}.tar.gz pcre2-${RESTY_PCRE_VERSION} \
openresty-${RESTY_VERSION}.tar.gz