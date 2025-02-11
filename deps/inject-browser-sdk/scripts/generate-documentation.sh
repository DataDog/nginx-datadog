#!/bin/bash
# Unless explicitly stated otherwise all files in this repository are licensed
# under the Apache 2.0 License. This product includes software developed at
# Datadog (https://www.datadoghq.com/).
#
# Copyright 2024-Present Datadog, Inc.

set -euo pipefail
# Generate Rust component documentation

HERE=$(PWD)
OUTPUT_DIR="${HERE}/doc"

BOLD_YELLOW='\033[1;33m'
RESET='\033[0m'

function usage {
  cat<<END_USAGE
generate-documentation.sh - Generate Rust library documentation

usage:
  generate-documentation.sh [-h] [--output-dir <DIR>]

options:
  -h, --help              Show this help messge and exit
  -o, --output-dir        Specify a directory to write the documentation into (default to ${OUTPUT_DIR})
END_USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    -h|--help)
      usage
      exit 0
      ;;
    -o|--output-dir)
      OUTPUT_DIR="$1"
      shift
      ;;
    *)
      usage
      exit 1
  esac
done

if [[ -d "$OUTPUT_DIR" ]]; then
  echo -e "${BOLD_YELLOW}warning${RESET}: directory ${OUTPUT_DIR} already exists and will be overwritten."
  rm -rf "${OUTPUT_DIR}"
fi

# Cargo generates documentation in a target directory. Instead of using the default target directory,
# we create a temporary directory, and then, move the generated documentation to the desired location.
WORK_DIR="$(mktemp -d)"

if [[ ! "$WORK_DIR" || ! -d "$WORK_DIR" ]]; then
  echo "Failed to create temporary directory"
  exit 1
fi

function cleanup {
  rm -rf "$WORK_DIR"
}

trap cleanup EXIT

cargo doc --workspace --document-private-items --no-deps --target-dir "${WORK_DIR}"

mv "${WORK_DIR}/doc" "${OUTPUT_DIR}"
