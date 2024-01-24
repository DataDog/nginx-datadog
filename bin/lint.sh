#!/bin/sh
# Lint the codebase and returns all encountered discrepencies
# with our codestyle

find src/ -type f \( -name '*.h' -o -name '*.cpp' \) -print0 | xargs -0 clang-format-14 --Werror --dry-run --style=file
find bin/ -type f -name '*.py' -print0 | xargs -0 yapf3 --diff

yapf3 --recursive --diff "$@" "test/"
