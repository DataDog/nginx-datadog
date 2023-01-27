#!/bin/sh

# Print to standard output the names of all of the tags associated with a
# specified DockerHub  image.  Include only those tags that contain a major,
# minor, and patch version, e.g.
#
#     $ bin/fetch_docker_tags.sh nginx | tail
#     1.21.4-alpine-perl
#     1.21.4-perl
#     1.21.5
#     1.21.5-alpine
#     1.21.5-alpine-perl
#     1.21.5-perl
#     1.21.6
#     1.21.6-alpine
#     1.21.6-alpine-perl
#     1.21.6-perl

set -e

IMAGE=${1:-nginx}

fetch_pages() {
    # Save each page of results to a temporary file so that we can then extract
    # the result's .next property.
    scratch=$(mktemp -d)
    out="$scratch/out"

    page=1
    next="https://hub.docker.com/v2/repositories/library/$IMAGE/tags/?page=$page"
    while [ "$next" != 'null' ]; do
        curl --silent "$next" | tee "$out"
        page=$((page + 1))
        next=$(<"$out" jq --raw-output .next)
    done

    rm -r "$scratch"
}

# Extract the tag names from the pages of JSON results,
# and filter to only <major>.<minor>.<patch>* tags.
fetch_pages | \
    jq --raw-output '.results[] | .name' | \
    grep '^[0-9]\+\.[0-9]\+\.[0-9]\+' | \
    sort --version-sort --reverse | \
    uniq
