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

lint_status=0

find "${source_directories[@]}" -type f \( -name '*.h' -o -name '*.cpp' -o -name '*.c' \) -print0 | xargs -0 clang-format-14 --Werror --dry-run --style=file
rc=$?
if [ "$rc" -ne 0 ]; then
    >&2 echo 'C++ formatter check failed.'
    lint_status=1
fi

find "${source_directories[@]}" -type f -name '*.py' -print0 | xargs -0 yapf --recursive --diff
rc=$?
if [ "$rc" -ne 0 ]; then
    >&2 echo 'Python formatter check failed.'
    lint_status=1
fi

exit "$lint_status"
