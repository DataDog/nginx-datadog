#!/bin/sh

set -e

install_nginx_on_amazon_linux() {
    amazon-linux-extras enable nginx1
    yum clean metadata
    yum install -y nginx
}

# `procps` contains `kill`, which is used to bring down temporary instances of
# nginx.
# Also, if we're on Amazon Linux, nginx won't be installed yet, so install it.
if command -v apt-get >/dev/null 2>&1; then
    apt-get update
    DEBIAN_FRONTEND=noninteractive apt-get install -y procps
    if ! command -v nginx >/dev/null 2>&1; then
        >&2 echo 'nginx must already be installed on Debian-flavored base images'
        exit 1
    fi
elif command -v apk >/dev/null 2>&1; then
    apk update
    apk add procps
    if ! command -v nginx >/dev/null 2>&1; then
        >&2 echo 'nginx must already be installed on Alpine-flavored base images'
        exit 1
    fi
elif command -v yum >/dev/null 2>&1; then
    yum update
    yum install -y procps
    if ! command -v nginx >/dev/null 2>&1; then
        install_nginx_on_amazon_linux
    fi
else
    >&2 printf 'Did not find a supported package manager.\n'
    exit 1
fi
