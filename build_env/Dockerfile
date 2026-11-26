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

# Follow the published internal client without introducing a Buildx version pin.
COPY --from=registry.ddbuild.io/images/docker:27.3.1 /usr/local/libexec/docker/cli-plugins/docker-buildx /usr/local/lib/docker/cli-plugins/docker-buildx
RUN selected_buildx="$(docker buildx version)" \
    && case "$selected_buildx" in \
      "github.com/docker/buildx v"*-dd.*" "*) printf '%s\n' "$selected_buildx" ;; \
      *) echo "Expected internal Buildx: $selected_buildx" >&2; exit 1 ;; \
    esac
