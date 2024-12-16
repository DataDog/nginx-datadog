#!/bin/sh

set -e

export RESTY_J="-j8"
export _RESTY_CONFIG_DEPS=""
export RESTY_CONFIG_OPTIONS=""
export RESTY_LUAJIT_OPTIONS=""
export RESTY_PCRE_OPTIONS=""

cd /tmp
curl -fSL https://openresty.org/download/openresty-"${RESTY_VERSION}".tar.gz -o openresty-"${RESTY_VERSION}".tar.gz
tar xzf openresty-"${RESTY_VERSION}".tar.gz
cd /tmp/openresty-"${RESTY_VERSION}"
eval CC=clang CXX=clang++ ./configure "${RESTY_J} ${_RESTY_CONFIG_DEPS} ${RESTY_CONFIG_OPTIONS} ${RESTY_LUAJIT_OPTIONS} ${RESTY_PCRE_OPTIONS}"
cd /tmp
rm -rf openresty-"${RESTY_VERSION}".tar.gz
