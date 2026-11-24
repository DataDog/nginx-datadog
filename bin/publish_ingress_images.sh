#!/bin/bash

# Publish the signed ingress-nginx injection images to Docker Hub.

set -euo pipefail

main() {
    if [ "$#" -ne 1 ]; then
        >&2 echo "usage: $0 <release|dev>"
        exit 1
    fi
    local mode="$1"
    : "${INGRESS_IMAGE_TAGS:?The signed image build must succeed first}"
    local image_tag
    for image_tag in ${INGRESS_IMAGE_TAGS}; do
        publish_image "${mode}" "${image_tag}"
    done
}

publish_image() {
    local mode="$1"
    local image_tag="$2"
    local tag_without_digest="${image_tag%@*}"
    local controller_version="${tag_without_digest%-*}"
    dd-pkg publish-image \
        --sources "${INGRESS_IMAGE_SOURCE_REPOSITORY}:${image_tag}" \
        --merge-strategy none \
        --destinations "$(list_destinations "${mode}" "${controller_version}")" \
        --registries dockerhub
}

list_destinations() {
    local mode="$1"
    local controller_version="$2"
    if [[ "${mode}" == release ]]; then
        echo "ingress-nginx-injection:${controller_version}-dd.${CI_COMMIT_TAG},ingress-nginx-injection:${controller_version}"
    else
        echo "ingress-nginx-injection-dev:${controller_version}-dev.${CI_COMMIT_SHORT_SHA}"
    fi
}

main "$@"
