#!/bin/sh

# Print lines:
# <nginx release version> <link to .tar.gz of that release>
# ...

downloads=https://nginx.org/download

filter() {
    if [ $# -eq 0 ]; then
        cat
    elif [ $# -eq 1 ]; then
        grep "^$(echo "$1" | sed 's/\./\\./g')[. ]" | tail -1 | awk '{print $2}'
    else
        >&2 printf 'At most one argument supported, but %d were specified.\n' $#
    fi
}

curl -s -S -L "$downloads" | \
    sed -n 's/^.*href="\([^"]\+\)".*$/\1/p' | \
    grep '^nginx-.*\.tar\.gz$' | \
    sed "s,^nginx-\(.*\)\.tar\.gz,\1 $downloads/\0," | \
    sort --version-sort | \
    filter "$@"
