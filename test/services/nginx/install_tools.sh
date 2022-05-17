#!/bin/sh

set -e

if command -v apt-get >/dev/null 2>&1; then
    apt-get update
    DEBIAN_FRONTEND=noninteractive apt-get install -y wget tar procps
elif command -v apk >/dev/null 2>&1; then
    apk update
    apk add wget tar procps
else
    >&2 printf 'Did not find a supported package manager.\n'
    exit 1
fi

# grpcurl is a self-contained binary (Go program)
cd /tmp
wget https://github.com/fullstorydev/grpcurl/releases/download/v1.8.6/grpcurl_1.8.6_linux_x86_64.tar.gz
tar -xzf grpcurl_1.8.6_linux_x86_64.tar.gz
mv grpcurl /usr/local/bin
rm grpcurl_1.8.6_linux_x86_64.tar.gz
