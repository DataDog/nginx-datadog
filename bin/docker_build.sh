#!/bin/sh

set -e

usage() {
    cat <<'END_USAGE'
docker_build.sh - Create a build image in Docker

usage:

    docker_build.sh [--yes] [--push] [<NGINX_TAG>]
        `docker build` an image suitable for building a Datadog nginx
        module compatible with the optionally specified NGINX_TAG.
        If NGINX_TAG is not specified, use the contents of the
        nginx-tag file.

        Prompt the user for confirmation unless --yes is specified.

        If --push is specified, push the resulting image to DockerHub
        with a suitable tag.

    docker_build.sh --help
    docker_build.sh -h
        Print this message.
END_USAGE
}

repo=$(dirname "$0")/..
yes=0
push=0
nginx_tag=''

while [ $# -ne 0 ]; do
    case "$1" in
    -h|--help) usage; exit ;;
    -y|--yes) yes=1 ;;
    -p|--push) push=1 ;;
    *)
        if [ -n "$nginx_tag" ]; then
            >&2 printf 'nginx tag was specified twice: first as %s and now as %s.' "$nginx_tag" "$1"
            exit 1
        fi
        nginx_tag="$1"
    esac
    shift
done

if [ -z "$nginx_tag" ]; then
    nginx_tag=$(cat "$repo/nginx-tag")
fi

ask() {
    if [ "$yes" -eq 1 ]; then
        return
    fi

    while true; do
        printf '%s [Yn]: ' "$1"
        read -r response
        case "$response" in
            n|N|no|NO|No) return 1 ;;
            y|Y|yes|YES|Yes|'') return ;;
            *) >&2 printf "\nI don't understand.\n"
        esac
    done
}

built_tag="nginx-datadog-build-$nginx_tag"
if ! ask "Build image compatible with nginx:$nginx_tag and tag as $built_tag?"; then
    exit 1
fi
docker build --build-arg "BASE_IMAGE=nginx:$nginx_tag" --tag "$built_tag" "$repo"

if [ "$push" -eq 0 ]; then
    exit
fi
destination="datadog/docker-library:nginx-datadog-build-$nginx_tag"
if ! ask "Push built image to \"$destination\"?"; then
    exit
fi

docker tag "$built_tag" "$destination"
docker push "$destination"
