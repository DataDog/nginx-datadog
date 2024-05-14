#!/bin/bash -e

function main {
  readonly expected_mod_version=$1

  local mod_version=
  mod_version=$(grep 'project(.*VERSION' CMakeLists.txt  | sed 's/.* \([0-9.]\+\)).*/\1/')

  if [[ $expected_mod_version != "$mod_version" ]]; then
    echo "datadog_version_ngx_mod mismatch: $expected_mod_version != $mod_version" >&2
    exit 1
  fi

  echo "All OK"
}

main "$1"
