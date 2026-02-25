#!/bin/sh
# Print any discrepancies between the formatting of the code and the expected
# style.

if [ -L .clang-format ] && ! [ -e .clang-format ]; then
    >&2 echo '.clang-format is a broken symlink. Initialize the dd-trace-cpp submodule: git submodule update --init dd-trace-cpp'
    exit 1
fi
if ! [ -e .clang-format ]; then
    >&2 echo '.clang-format file is missing. Run "make lint".'
    exit 1
fi

error_messages=''

find src/ -type f \( -name '*.h' -o -name '*.cpp' \) -print0 | xargs -0 clang-format-14 --Werror --dry-run --style=file
rc=$?
if [ "$rc" -ne 0 ]; then
    error_messages=$(printf '%s\nC++ formatter reported formatting differences in src/ and returned error status %d.\n' "$error_messages" "$rc")
fi

find bin/ -type f -name '*.py' -print0 | xargs -0 yapf3 --diff
rc=$?
if [ "$rc" -ne 0 ]; then
    error_messages=$(printf '%s\nPython formatter reported formatting differences in bin/ and returned error status %d.\n' "$error_messages" "$rc")
fi

yapf3 --recursive --diff "$@" "test/"
rc=$?
if [ "$rc" -ne 0 ]; then
    error_messages=$(printf '%s\nPython formatter reported formatting differences in test/ and returned error status %d.\n' "$error_messages" "$rc")
fi

if [ -n "$error_messages" ]; then
    >&2 echo "$error_messages"
    exit 1
fi
