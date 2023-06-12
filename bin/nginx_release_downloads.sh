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

if [ "$(uname)" = 'Darwin' ]; then
  SED='gsed'
else
  SED='sed'
fi

if [ $# -gt 1 ]; then
    >&2 printf 'At most one argument supported, but %d were specified.\n' $#
    exit 1
fi

filter() {
    if [ $# -eq 1 ]; then
        grep "^$(echo "$1" | $SED 's/\./\\./g')[. ]" | tail -1 | awk '{print $2}'
    else
        cat
    fi
}

curl -s -S -L "$downloads" | \
    $SED -n 's/^.*href="\([^"]\+\)".*$/\1/p' | \
    grep '^nginx-.*\.tar\.gz$' | \
    $SED "s,^nginx-\(.*\)\.tar\.gz,\1 $downloads/\0," | \
    sort --version-sort | \
    filter "$@"
