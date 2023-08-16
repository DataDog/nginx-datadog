#!/bin/sh

# This script is a wrapper around "curl".
#
# It passes its command line arguments to "curl," but has the following
# additional behavior:
#
# - The progress meter is suppressed.
# - The first line of standard output is the JSON object produced by the
#   "--write-out ${json}" option.
# - The second line of standard input is the response body represented as
#   a JSON string, i.e. double quoted and with escape sequences.
#
# The intention is that the test driver invoke this script using
# `docker compose exec`.  The output format is chosen to be easy to parse in
# Python.

tmpdir=$(mktemp -d)
body_file="$tmpdir"/json
touch "$body_file"

# Print information about the response as a JSON object on one line.
curl --write-out '%{json}' --output "$body_file" --no-progress-meter "$@"
status=$?

printf '\n'
# Print the contents of $body_file as one quoted JSON string on one line.
<"$body_file" jq --raw-input --slurp .

rm -r "$tmpdir" >&2
exit $status
