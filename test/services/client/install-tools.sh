#!/bin/sh
set -e

ARCH="$(uname -m)"

apk update
apk add wget tar curl jq

# grpcurl is a self-contained binary (Go program)
cd /tmp
wget https://github.com/fullstorydev/grpcurl/releases/download/v1.8.6/grpcurl_1.8.6_linux_${ARCH}.tar.gz
tar -xzf grpcurl_1.8.6_linux_${ARCH}.tar.gz
mv grpcurl /usr/local/bin
rm grpcurl_1.8.6_linux_${ARCH}.tar.gz
