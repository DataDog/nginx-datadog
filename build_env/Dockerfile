# check=skip=InvalidDefaultArgInFrom
ARG MUSL_TOOLCHAIN_IMAGE
FROM ${MUSL_TOOLCHAIN_IMAGE}

ARG MUSL_TOOLCHAIN_IMAGE
LABEL org.opencontainers.image.base.name="${MUSL_TOOLCHAIN_IMAGE}"

COPY install-rum-build-tools /usr/local/bin/install-rum-build-tools

RUN apk add --no-cache \
      curl \
      docker-cli \
      docker-cli-buildx \
      docker-cli-compose \
      nodejs \
      npm \
      shellcheck \
      uv && \
    install-rum-build-tools && \
    rm -rf /root/.cargo/registry /root/.cargo/git
