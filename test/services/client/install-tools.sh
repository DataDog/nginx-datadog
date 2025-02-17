#!/bin/sh
set -e

case "$(uname -m)" in
  aarch64)
    ARCH="arm64"
    ;;
  *)
    ARCH="$(uname -m)"
    ;;
esac

apk update
apk add wget tar jq

# grpcurl is a self-contained binary (Go program)
GRPCURL_TAR="grpcurl_1.8.6_linux_${ARCH}.tar.gz"

cd /tmp
wget https://github.com/fullstorydev/grpcurl/releases/download/v1.8.6/"${GRPCURL_TAR}"
tar -xzf "${GRPCURL_TAR}"
mv grpcurl /usr/local/bin
rm "${GRPCURL_TAR}"
