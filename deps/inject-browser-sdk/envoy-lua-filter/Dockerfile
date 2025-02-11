# Unless explicitly stated otherwise all files in this repository are licensed
# under the Apache 2.0 License. This product includes software developed at
# Datadog (https://www.datadoghq.com/).
#
# Copyright 2024-Present Datadog, Inc.

ARG ENVOY_IMAGE="${ENVOY_IMAGE:-envoyproxy/envoy}"
ARG ENVOY_VARIANT="${ENVOY_VARIANT:-dev}"


FROM ${ENVOY_IMAGE}:${ENVOY_VARIANT}
ARG ENVOY_CONFIG=envoy.yaml
ENV ENVOY_CONFIG="$ENVOY_CONFIG"
ENV DEBIAN_FRONTEND=noninteractive
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt/lists,sharing=locked \
    rm -f /etc/apt/apt.conf.d/docker-clean \
    && echo 'Binary::apt::APT::Keep-Downloaded-Packages "true";' | tee /etc/apt/apt.conf.d/keep-cache \
    && apt-get -qq update -y \
    && apt-get -qq install --no-install-recommends -y curl

ADD  --chmod=777 ./src/datadog_rum.lua /usr/local/lib/datadog_rum.lua

COPY --chmod=777 "$ENVOY_CONFIG" /etc/envoy.yaml

COPY --chmod=644 ./static/index.html /var/www/index.html

CMD ["/usr/local/bin/envoy", "-c", "/etc/envoy.yaml"]
