#!/usr/bin/env bash
# Unless explicitly stated otherwise all files in this repository are licensed
# under the Apache 2.0 License. This product includes software developed at
# Datadog (https://www.datadoghq.com/).
#
# Copyright 2024-Present Datadog, Inc.

set -euo pipefail
# Enforce codestyle

function usage {
  cat<<'END_USAGE'
codestyle.sh - Enforce codestyle

usage:
  codestyle.sh [-h] {lint, format}

options:
  -h, --help              Show this help messge and exit

subcommand:
  lint: 
  format: Format in-place
END_USAGE
}

function lint {
  cargo fmt --all -- --check
  cargo clippy --all-targets --all-features
}

function format {
  cargo fmt --all
  cargo clippy --fix --allow-dirty
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    -h|--help)
      usage
      exit 0
      ;;
    format)
      format
      exit 0
      ;;
    lint)
      lint
      exit 0
      ;;
    *)
      usage
      exit 1
  esac
done
