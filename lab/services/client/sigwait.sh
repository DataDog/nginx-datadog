#!/bin/sh

usage() {
    cat <<END_USAGE
sigwait.sh - Block until a specified signal is received.

usage:
    sigwait.sh [SIGNAL] ...
        Block until one of the specified SIGNALs is received.
        Each SIGNAL is named as would appear in a call to
        "trap", e.g. QUIT, TERM, STOP.

    sigwait.sh --help
    sigwait.sh -h
        Print this message.
END_USAGE
}

if [ $# -eq 0 ]; then
    >&2 usage
    exit 1
fi

for arg in "$@"; do
    case "$arg" in
    --help|-h)
        usage
        exit 0 ;;
    *) ;;
    esac
done

tmpdir=$(mktemp -d)
pipe="$tmpdir"/pipe
mkfifo "$pipe"

close() {
    trap - "$@"
    echo 'cya!' >"$pipe"
}

block() {
    <"$pipe" read -r
}

trap close "$@"

block &
wait
rm -r "$tmpdir"
wait
exit 0
