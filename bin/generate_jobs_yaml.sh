#!/bin/sh

# Print out a snippet that will go in .circleci/config.yaml
#
# It describes a "build" and "test" job for each supported nginx tag.

set -e

indentation='    '

REPO=$(dirname "$(dirname "$(realpath "$0")")")
cd "$REPO"

# Here are the supported nginx tags.
supported_nginx_tags=$(mktemp)
>"$supported_nginx_tags" cat <<END_NGINX_TAGS
1.23.2-alpine
1.23.2
1.23.1-alpine
1.23.1
1.23.0-alpine
1.23.0
1.22.1-alpine
1.22.1
1.22.0-alpine
1.22.0
1.21.6-alpine
1.21.6
1.21.5-alpine
1.21.5
1.21.4-alpine
1.21.4
1.21.3-alpine
1.21.3
1.21.1-alpine
1.21.1
1.21.0-alpine
1.21.0
1.20.2-alpine
1.20.2
1.20.1-alpine
1.20.1
1.20.0-alpine
1.20.0
1.19.10-alpine
1.19.10
1.19.9-alpine
1.19.9
1.19.8-alpine
1.19.8
1.19.7-alpine
1.19.7
1.19.6-alpine
1.19.6
1.19.5-alpine
1.19.5
1.19.4-alpine
1.19.4
1.19.3-alpine
1.19.3
1.19.2-alpine
1.19.2
1.19.1-alpine
1.19.1
1.19.0-alpine
1.19.0
1.18.0-alpine
1.18.0
1.17.10-alpine
1.17.10
1.17.9-alpine
1.17.9
1.17.8-alpine
1.17.8
1.17.7-alpine
1.17.7
1.17.6-alpine
1.17.6
1.17.5-alpine
1.17.5
1.17.4-alpine
1.17.4
1.17.3-alpine
1.17.3
1.17.2-alpine
1.17.2
1.17.1-alpine
1.17.1
1.17.0-alpine
1.17.0
1.16.1-alpine
1.16.1
1.16.0-alpine
1.16.0
END_NGINX_TAGS

while read nginx_tag; do
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
done <"$supported_nginx_tags" | sed "s/^/$indentation/"

rm "$supported_nginx_tags"
