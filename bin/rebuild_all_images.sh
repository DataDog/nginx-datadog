#!/bin/sh

# When all of the build images need to change, we need to enumerate all
# supported base images, and for each build the corresponding build image
# and publish it to DockerHub.

set -x

base_images() {
    bin/generate_jobs_yaml.sh | \
        yaml2json | \
        jq --raw-output '.[] | to_entries[] | .value["base-image"]' | \
        sort --version-sort | \
        uniq
}

bin=$(dirname "$0")

base_images | while read -r base_image; do
    echo "Building build image for base image $base_image"
    "$bin"/docker_build.sh --platform linux/amd64,linux/arm64 --yes --push "$base_image"
done
