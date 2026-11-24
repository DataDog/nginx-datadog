#!/bin/bash

# Build the ingress-nginx injection images in the internal registry.
# Sign and verify each multi-architecture digest.
# Write the signed image tags to the given output file.

set -euo pipefail

image_tags=()

main() {
    if [ "$#" -ne 1 ]; then
        >&2 echo "usage: $0 <OUTPUT_ENV_FILE>"
        exit 1
    fi
    local output_file="$1"
    local version
    for version in $(list_versions); do
        build_and_sign_image "${version}"
    done
    printf 'INGRESS_IMAGE_TAGS=%s\n' "${image_tags[*]}" > "${output_file}"
}

list_versions() {
    printf '%s\n' artifacts-ingress/{amd64,arm64}/* | cut -d/ -f3 | sort -u
}

build_and_sign_image() {
    local version="$1"
    local image_tag="v${version}-${CI_JOB_ID}"
    local image="${INGRESS_IMAGE_SOURCE_REPOSITORY}:${image_tag}"
    local arch
    for arch in amd64 arm64; do
        build_and_push_arch_image "${image}" "${version}" "${arch}"
    done
    docker buildx imagetools create --tag "${image}" \
        "${image}-amd64" "${image}-arm64"
    local digest
    digest="$(crane digest "${image}")"
    ddsign sign "${image}@${digest}"
    ddsign verify --roles=ci "${image}@${digest}"
    image_tags+=("${image_tag}@${digest}")
}

build_and_push_arch_image() {
    local image="$1"
    local version="$2"
    local arch="$3"
    local module_context="artifacts-ingress/${arch}/${version}"
    test -f "${module_context}/ngx_http_datadog_module.so"
    docker build --progress=plain --no-cache \
        --provenance=false --sbom=false \
        --build-context "build=${module_context}" \
        --platform "linux/${arch}" --tag "${image}-${arch}" \
        injection/ingress-nginx
    docker push "${image}-${arch}"
}

main "$@"
