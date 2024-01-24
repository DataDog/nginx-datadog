#!/bin/sh
# Format the codebase to follow our codestyle

find src/ -type f \( -name '*.h' -o -name '*.cpp' \) -print0 | xargs -0 clang-format-14 -i --style=file
find bin/ -type f -name '*.py' -print0 | xargs -0 yapf3 -i

yapf3 --recursive --in-place "$@" "test/"
