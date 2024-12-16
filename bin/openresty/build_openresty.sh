export RESTY_VERSION="1.27.1.1"
export NGINX_VERSION="1.27.1"

export RESTY_J="-j8"

_RESTY_CONFIG_DEPS=""
RESTY_CONFIG_OPTIONS=""
RESTY_LUAJIT_OPTIONS=""
RESTY_PCRE_OPTIONS=""

cd /tmp
curl -fSL https://openresty.org/download/openresty-${RESTY_VERSION}.tar.gz -o openresty-${RESTY_VERSION}.tar.gz
tar xzf openresty-${RESTY_VERSION}.tar.gz
cd /tmp/openresty-${RESTY_VERSION}
eval CC=clang CXX=clang++ ./configure ${RESTY_J} ${_RESTY_CONFIG_DEPS} ${RESTY_CONFIG_OPTIONS} ${RESTY_LUAJIT_OPTIONS} ${RESTY_PCRE_OPTIONS}
cd /tmp
rm -rf openresty-${RESTY_VERSION}.tar.gz
