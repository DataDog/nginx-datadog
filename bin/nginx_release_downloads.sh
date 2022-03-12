#!/bin/sh

# Print lines:
#
# <nginx release version> <link to .tar.gz of that release>
# ...

# Usage:
#
#     bin/nginx_release_downloads [VERSION]
#
# If VERSION is specified, then the download link of the most recent matching
# release will be printed instead of all releases with versions.

downloads=https://nginx.org/download

if [ $# -gt 1 ]; then
    >&2 printf 'At most one argument supported, but %d were specified.\n' $#
    exit 1
fi

filter() {
    if [ $# -eq 1 ]; then
        grep "^$(echo "$1" | sed 's/\./\\./g')[. ]" | tail -1 | awk '{print $2}'
    else
        cat
    fi
}

curl -s -S -L "$downloads" | \
    sed -n 's/^.*href="\([^"]\+\)".*$/\1/p' | \
    grep '^nginx-.*\.tar\.gz$' | \
    sed "s,^nginx-\(.*\)\.tar\.gz,\1 $downloads/\0," | \
    sort --version-sort | \
    filter "$@"
