#!/bin/sh

# usage:
#
#     $ base_image_from_nginx_tag.sh <NGINX_TAG>
#
# Print to standard output the name:tag of a base image appropriate for
# building the version of nginx in the nginx image having the specified
# NGINX_TAG.
#
# For example,
#
#     $ base_image_from_nginx_tag.sh 1.15.12-alpine
#     alpine:3.9.4
#     $ base_image_from_nginx_tag.sh 1.14
#     debian:9-slim

# /etc/os-release defines environment (shell) variables that describe the
# system.
eval "$(docker run --rm --entrypoint=/bin/cat "nginx:$1" /etc/os-release)"

# It's either Debian or Alpine.  We can tell which by looking at the beginning
# of $NAME.  Then, either way, the version is $VERSION_ID, except that we
# append "-slim" to the Debian tag (to get a smaller image, as nginx does).
case "$NAME" in
    "Debian "*)   echo "debian:$VERSION_ID-slim" ;;
    "Alpine "*)   echo "alpine:$VERSION_ID" ;;
    *)  >&2 echo "Unexpected nginx image system name: $NAME"
        exit 1 ;;
esac
