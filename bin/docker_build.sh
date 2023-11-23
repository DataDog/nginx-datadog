#!/bin/sh
set -e

usage() {
    cat <<'END_USAGE'
docker_build.sh - Create a build image in Docker

usage:

    docker_build.sh --platforms <PLATFORM> [--yes] [--push] [<BASE_IMAGE>]
        `docker build` an image suitable for building a Datadog nginx
        module compatible with the optionally specified BASE_IMAGE.
        If BASE_IMAGE is not specified, use the contents of the
        nginx-version-info file.

        Prompt the user for confirmation unless --yes is specified.

        --platforms is a comma separated list of platforms to target.
        --platforms is required. The option has the same format as --platform
        in `docker buildx build`.
        For example: --platforms linux/amd64,linux/arm64

        If --push is specified, push the resulting image to DockerHub
        with a suitable tag.

    docker_build.sh --help
    docker_build.sh -h
        Print this message.
END_USAGE
}

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

repo=$(dirname "$0")/..
yes=0
push=0
platforms=''
base_image=''

while [ $# -ne 0 ]; do
    case "$1" in
    -h|--help)
      usage
      exit
      ;;
    -y|--yes)
      yes=1
      ;;
    -p|--push)
      push=1
      ;;
    --platforms)
      platforms="$2"
      shift
      ;;
    *)
        if [ -n "$base_image" ]; then
            >&2 printf 'base image was specified twice: first as %s and now as %s.' "$base_image" "$1"
            exit 1
        fi
        base_image="$1"
    esac
    shift
done

if [ -z "$base_image" ]; then
    # shellcheck disable=SC1091
    . "$repo/nginx-version-info"
    # shellcheck disable=SC2153
    base_image="$BASE_IMAGE"
fi

if [ -z "$platforms" ]; then
  >&2 printf 'missing required option: --platforms\n'
  usage
  exit
fi

base_image_without_colons=$(echo "$base_image" | tr ':' '_')
built_tag="nginx-datadog-build-$base_image_without_colons"

remote_destination="datadog/docker-library:$built_tag"
buildx_output_args="--output=type=image,name=${remote_destination},push=true"

if ! ask "Build image compatible with ${base_image} for ${platforms} and tag as ${built_tag}?"; then
    exit 1
fi

if [ "$push" -eq 0 ]; then
    if ! ask "Push built image to \"${remote_destination}\"?"; then
        buildx_output_args="--output=type=image,name=${built_tag},push=false"
    fi
fi

docker buildx build \
  --platform "${platforms}" \
  --build-arg "BASE_IMAGE=${base_image}" \
  "${buildx_output_args}" \
  "${repo}"
