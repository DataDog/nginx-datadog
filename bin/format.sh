#!/bin/bash
# Format the code

set -eo pipefail

source_directories=(bin src test tools)

if [ -L .clang-format ] && ! [ -e .clang-format ]; then
    >&2 echo '.clang-format is a broken symlink. Initialize the dd-trace-cpp submodule: git submodule update --init dd-trace-cpp'
    exit 1
fi
if ! [ -e .clang-format ]; then
    >&2 echo '.clang-format file is missing. Initialize the dd-trace-cpp submodule: git submodule update --init dd-trace-cpp'
    exit 1
fi

find "${source_directories[@]}" -type f \( -name '*.h' -o -name '*.cpp' -o -name '*.c' \) -print0 | xargs -0 clang-format-14 -i --style=file
find "${source_directories[@]}" -type f -name '*.py' -print0 | xargs -0 yapf --recursive --in-place
