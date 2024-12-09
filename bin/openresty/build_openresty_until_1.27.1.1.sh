#!/bin/sh
set -e

export RESTY_OPENSSL_PATCH_VERSION="1.1.1f"
export RESTY_OPENSSL_URL_BASE="https://www.openssl.org/source/old/1.1.1"
export RESTY_PCRE_VERSION="8.45"
export RESTY_PCRE_BUILD_OPTIONS="--enable-jit"
export RESTY_PCRE_SHA256="4e6ce03e0336e8b4a3d6c2b70b1c5e18590a5673a98186da90d4f33c23defc09"
export RESTY_J=8
export RESTY_CONFIG_OPTIONS="\
    --with-compat \
    --with-file-aio \
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
    --with-http_xslt_module=dynamic \
    --with-ipv6 \
    --with-mail \
    --with-mail_ssl_module \
    --with-md5-asm \
    --with-sha1-asm \
    --with-stream \
    --with-stream_ssl_module \
    --with-threads \
    "
export RESTY_CONFIG_OPTIONS_MORE=""
export RESTY_LUAJIT_OPTIONS="--with-luajit-xcflags='-DLUAJIT_NUMMODE=2 -DLUAJIT_ENABLE_LUA52COMPAT'"
export RESTY_PCRE_OPTIONS="--with-pcre-jit"

export RESTY_ADD_PACKAGE_BUILDDEPS=""
export RESTY_ADD_PACKAGE_RUNDEPS=""
export RESTY_EVAL_PRE_CONFIGURE=""
export RESTY_EVAL_POST_DOWNLOAD_PRE_CONFIGURE=""
export RESTY_EVAL_POST_MAKE=""

# These are not intended to be user-specified
export _RESTY_CONFIG_DEPS="--with-pcre \
    --with-cc-opt='-DNGX_LUA_ABORT_AT_PANIC -I/usr/local/openresty/pcre/include -I/usr/local/openresty/openssl/include' \
    --with-ld-opt='-L/usr/local/openresty/pcre/lib -L/usr/local/openresty/openssl/lib -Wl,-rpath,/usr/local/openresty/pcre/lib:/usr/local/openresty/openssl/lib' \
    "

cd /tmp
if [ -n "${RESTY_EVAL_PRE_CONFIGURE}" ]; then eval $(echo ${RESTY_EVAL_PRE_CONFIGURE}); fi
curl -fSL "${RESTY_OPENSSL_URL_BASE}/openssl-${RESTY_OPENSSL_VERSION}.tar.gz" -o openssl-${RESTY_OPENSSL_VERSION}.tar.gz
tar xzf openssl-${RESTY_OPENSSL_VERSION}.tar.gz
cd openssl-${RESTY_OPENSSL_VERSION}
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
    no-threads shared zlib -g \
    enable-ssl3 enable-ssl3-method \
    --prefix=/usr/local/openresty/openssl \
    --libdir=lib \
    -Wl,-rpath,/usr/local/openresty/openssl/lib
make -j${RESTY_J}
make -j${RESTY_J} install_sw
cd /tmp
curl -fSL https://downloads.sourceforge.net/project/pcre/pcre/${RESTY_PCRE_VERSION}/pcre-${RESTY_PCRE_VERSION}.tar.gz -o pcre-${RESTY_PCRE_VERSION}.tar.gz
echo "${RESTY_PCRE_SHA256}  pcre-${RESTY_PCRE_VERSION}.tar.gz" | shasum -a 256 --check
tar xzf pcre-${RESTY_PCRE_VERSION}.tar.gz
cd /tmp/pcre-${RESTY_PCRE_VERSION}
CC=clang ./configure \
    --prefix=/usr/local/openresty/pcre \
    --disable-cpp \
    --enable-utf \
    --enable-unicode-properties \
    ${RESTY_PCRE_BUILD_OPTIONS}
make -j${RESTY_J}
make -j${RESTY_J} install
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
rm -rf \
    openssl-${RESTY_OPENSSL_VERSION}.tar.gz openssl-${RESTY_OPENSSL_VERSION} \
    pcre-${RESTY_PCRE_VERSION}.tar.gz pcre-${RESTY_PCRE_VERSION} \
    openresty-${RESTY_VERSION}.tar.gz
