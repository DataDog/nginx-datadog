#!/bin/sh
set -e

export RESTY_VERSION=$1

apk add --no-cache perl-dev libxslt-dev gd-dev geoip-dev

case $RESTY_VERSION in
    "1.21.4.1")
        export NGINX_VERSION="1.21.4"
        export RESTY_OPENSSL_VERSION="1.1.1u"
        ./bin/openresty/build_openresty_until_1.27.1.1.sh
    ;;

    "1.21.4.2" | "1.21.4.3" | "1.21.4.4")
        export NGINX_VERSION="1.21.4" && \
        export RESTY_OPENSSL_VERSION="1.1.1w" && \
        ./bin/openresty/build_openresty_until_1.27.1.1.sh
    ;;

    "1.25.3.1" | "1.25.3.2")
        export NGINX_VERSION="1.25.3" && \
        export RESTY_OPENSSL_VERSION="1.1.1w" && \
        export RESTY_CONFIG_OPTIONS_MORE="--with-http_v3_module"
        ./bin/openresty/build_openresty_until_1.27.1.1.sh
    ;;

    "1.27.1.1")
        export NGINX_VERSION="1.27.1.1"
        ./bin/openresty/build_openresty_1.27.1.1.sh
    ;;

esac