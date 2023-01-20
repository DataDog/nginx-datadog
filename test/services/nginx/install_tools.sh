#!/bin/sh

set -e

install_nginx_on_amazon_linux() {
    amazon-linux-extras enable -y nginx1
    yum install -y nginx
}

# `procps` contains `kill`, which is used to bring down temporary instances of
# nginx.
# Also, if we're on Amazon Linux, nginx won't be installed yet, so install it.
if command -v apt-get >/dev/null 2>&1; then
    # If this is Debian "stretch," then we need to change the package
    # repository links as of April 2023.
    . /etc/os-release
    if [ "$PRETTY_NAME" = 'Debian GNU/Linux 9 (stretch)' ]; then
        echo 'deb http://archive.debian.org/debian stretch main contrib non-free' >/etc/apt/sources.list
    fi
    apt-get update
    DEBIAN_FRONTEND=noninteractive apt-get install -y procps pstack
    if ! command -v nginx >/dev/null 2>&1; then
        >&2 echo 'nginx must already be installed on Debian-flavored base images'
        exit 1
    fi
elif command -v apk >/dev/null 2>&1; then
    apk update
    apk add procps gdb
    if ! command -v nginx >/dev/null 2>&1; then
        >&2 echo 'nginx must already be installed on Alpine-flavored base images'
        exit 1
    fi
elif command -v yum >/dev/null 2>&1; then
    yum update -y
    yum install -y procps pstack
    if ! command -v nginx >/dev/null 2>&1; then
        install_nginx_on_amazon_linux
    fi
else
    >&2 printf 'Did not find a supported package manager.\n'
    exit 1
fi
