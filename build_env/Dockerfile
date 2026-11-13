# check=skip=InvalidDefaultArgInFrom
ARG MUSL_TOOLCHAIN_IMAGE
FROM ${MUSL_TOOLCHAIN_IMAGE}

ARG MUSL_TOOLCHAIN_IMAGE
LABEL org.opencontainers.image.base.name="${MUSL_TOOLCHAIN_IMAGE}"

COPY install-rum-build-tools /usr/local/bin/install-rum-build-tools

RUN apk add --no-cache \
      curl \
      docker-cli \
      docker-cli-compose \
      nodejs \
      npm \
      openssl-libs-static \
      pcre-static \
      pcre2-static \
      shellcheck \
      uv \
      zlib-static && \
    install-rum-build-tools && \
    rm -rf /root/.cargo/registry /root/.cargo/git

ARG TARGETARCH
RUN set -eu; \
    case "$TARGETARCH" in \
      amd64) buildx_sha256=d974b718dd988bfe661346e8e2ac5751be55475883ae796ae9dea9d0f6cbe0b2 ;; \
      arm64) buildx_sha256=b22f3da81c913eb76da05143ba49ec3de06224b617161c969c43022add397b29 ;; \
      *) echo "Unsupported Buildx architecture: $TARGETARCH" >&2; exit 1 ;; \
    esac; \
    buildx_tmp="$(mktemp -d)"; \
    trap 'rm -rf "$buildx_tmp"' EXIT; \
    curl --fail --show-error --location --retry 3 -o "$buildx_tmp/buildx.tar.gz" "https://depot-read-api-github-releases.us1.ddbuild.io/internal/mirror/github-releases/DataDog/buildx/releases/download/v0.38.2-dd.3/buildx_0.38.2-dd.3_linux_${TARGETARCH}.tar.gz"; \
    echo "$buildx_sha256  $buildx_tmp/buildx.tar.gz" | sha256sum -c -; \
    tar -xzf "$buildx_tmp/buildx.tar.gz" -C "$buildx_tmp"; \
    mkdir -p /usr/local/lib/docker/cli-plugins; \
    install -m 0755 "$buildx_tmp/buildx" /usr/local/lib/docker/cli-plugins/docker-buildx; \
    docker buildx version; \
    test "$(docker buildx version | cut -d ' ' -f 2)" = "v0.38.2-dd.3"
