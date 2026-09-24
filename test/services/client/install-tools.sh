#!/bin/sh
set -e

case "$(uname -m)" in
  aarch64)
    ARCH="arm64"
    ARCH_WEBSOCAT=aarch64
    ;;
  *)
    ARCH="$(uname -m)"
    ARCH_WEBSOCAT=$ARCH
    ;;
esac

export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y --no-install-recommends \
  ca-certificates \
  curl \
  jq \
  netcat-openbsd \
  python3 \
  tar \
  wget
rm -rf /var/lib/apt/lists/*

curl --version | grep -q 'ngtcp2/'

# grpcurl is a self-contained binary (Go program)
GRPCURL_TAR="grpcurl_1.8.6_linux_${ARCH}.tar.gz"

cd /tmp
wget https://github.com/fullstorydev/grpcurl/releases/download/v1.8.6/"${GRPCURL_TAR}"
tar -xzf "${GRPCURL_TAR}"
mv grpcurl /usr/local/bin
rm "${GRPCURL_TAR}"

wget -O /usr/local/bin/websocat \
  https://github.com/vi/websocat/releases/download/v4.0.0-alpha2/websocat."${ARCH_WEBSOCAT}"-unknown-linux-musl
chmod +x /usr/local/bin/websocat
