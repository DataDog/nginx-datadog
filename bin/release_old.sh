#!/bin/sh

# Create a github draft prelease.

set -e

REPO=$(dirname "$(dirname "$(realpath "$0")")")
cd "$REPO"
release_tag=$(<release.json jq --raw-output .release_tag)

build_module() {
    printf '%s' "$nginx_tag" >nginx-tag
    
    # `docker build` the build image, if it isn't already built.
    bin/docker_build.sh --yes
    # clean slate
    make clobber
    # Build the nginx module in the build image created above.
    make build-in-docker
    cp .docker-build/libngx_http_datadog_module.so .release/ngx_http_datadog_module.so
    cd .release
    tar cvzf "${nginx_tag}-ngx_http_datadog_module.so.tgz" ngx_http_datadog_module.so
    gpg --armor --detach-sign "${nginx_tag}-ngx_http_datadog_module.so.tgz"
    rm ngx_http_datadog_module.so
    cd -
}

# Build one module per nginx tag listed in $REPO/release.json
# But first, warn and prompt if there are files in .release/
mkdir -p .release
if [ "$(ls .release | wc -l)" -ne 0 ]; then
    while true; do
        printf '.release/ is not empty.  Delete contents first?\n'
        ls .release
        printf ' [yN] '
        read response
        case "$response" in
          y|Y|yes|YES) rm .release/*; break ;;
          n|N|no|NO|"") break ;;
          *) printf "\nI don't understand.\n" ;;
        esac
    done
fi

<release.json jq --raw-output .nginx_tags[] | { while read nginx_tag || [ -n "$nginx_tag" ]; do
    build_module "$nginx_tag"
done }

cd .release
gh release create --prerelease --draft --title "$release_tag" --notes 'TODO' "$release_tag" *.tgz *.asc
