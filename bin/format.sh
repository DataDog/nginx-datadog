#!/bin/sh
# Format the codebase to follow this project's style.

if [ -L .clang-format ] && ! [ -e .clang-format ]; then
    >&2 echo '.clang-format is a broken symlink. Initialize the dd-trace-cpp submodule: git submodule update --init dd-trace-cpp'
    exit 1
fi
if ! [ -e .clang-format ]; then
    >&2 echo '.clang-format file is missing. Run "make format".'
    exit 1
fi

find src/ -type f \( -name '*.h' -o -name '*.cpp' -o -name '*.c' \) -print0 | xargs -0 clang-format-14 -i --style=file
find bin/ -type f -name '*.py' -print0 | xargs -0 yapf3 -i

yapf3 --recursive --in-place "$@" "test/"
