#!/bin/bash
# Print any discrepancies between the formatting of the code and the expected style. Collects all
# errors before exiting (no set -e).

set -o pipefail

source_directories=(bin src test tools)

if [ -L .clang-format ] && ! [ -e .clang-format ]; then
    >&2 echo '.clang-format is a broken symlink. Initialize the dd-trace-cpp submodule: git submodule update --init dd-trace-cpp'
    exit 1
fi
if ! [ -e .clang-format ]; then
    >&2 echo '.clang-format file is missing. Initialize the dd-trace-cpp submodule: git submodule update --init dd-trace-cpp'
    exit 1
fi

error_messages=''

find "${source_directories[@]}" -type f \( -name '*.h' -o -name '*.cpp' -o -name '*.c' \) -print0 | xargs -0 clang-format-14 --Werror --dry-run --style=file
rc=$?
if [ "$rc" -ne 0 ]; then
    error_messages=$(printf '%s\nC++ formatter reported formatting differences and returned error status %d.\n' "$error_messages" "$rc")
fi

find "${source_directories[@]}" -type f -name '*.py' -print0 | xargs -0 yapf --recursive --diff
rc=$?
if [ "$rc" -ne 0 ]; then
    error_messages=$(printf '%s\nPython formatter reported formatting differences and returned error status %d.\n' "$error_messages" "$rc")
fi

if [ -n "$error_messages" ]; then
    >&2 echo "$error_messages"
    exit 1
fi
