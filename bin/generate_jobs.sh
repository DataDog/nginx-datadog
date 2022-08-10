#!/bin/sh

# Print out a snippet that will go in .circleci/config.yaml
#
# It describes a "build" and "test" job for each nginx tag in release.json

indentation='    '

REPO=$(dirname "$(dirname "$(realpath "$0")")")
cd "$REPO"

<release.json jq --raw-output .nginx_tags[] | { while read nginx_tag || [ -n "$nginx_tag" ]; do
    cat <<END_SNIPPET
- build:
    name: "build $nginx_tag"
    build-image: "datadog/docker-library:nginx-datadog-build-$nginx_tag"
    nginx-tag: "$nginx_tag"
    filters:
      tags:
        only: /^v[0-9]+\.[0-9]+\.[0-9]+/
- test:
    name: "test $nginx_tag"
    requires:
    - "build $nginx_tag"
    filters:
      tags:
        only: /^v[0-9]+\.[0-9]+\.[0-9]+/
END_SNIPPET
done } | sed "s/^/$indentation/"
