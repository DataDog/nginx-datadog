#!/bin/sh
set -e
# NGINX Datadog's Module for Linux installation script.
#
# This script is intended as a convenient way to install
# and configure Datadog's Module for NGINX.
#
# Pre-requisites:
#
#  - Attempts to detect your NGINX version and configure your
#   package management system for you.
#
# Usage
# ==============================================================================
#
# To install the latest stable versions of Docker CLI, Docker Engine, and their
# dependencies:
#
# 1. download the script
#
#   $ curl -fsSL https://get.docker.com -o install-docker.sh
#
# 2. verify the script's content
#
#   $ cat install-docker.sh
#
# 3. run the script with --dry-run to verify the steps it executes
#
#   $ sh install-docker.sh --dry-run
#
# 4. run the script either as root, or using sudo to perform the installation.
#
#   $ sudo sh install-docker.sh
#
# Command-line options
# ==============================================================================
#
# --version <VERSION>
# Use the --version option to install a specific version, for example:
#
#   $ sudo sh install-docker.sh --version 23.0
#
# --channel <stable|test>
#
# Use the --channel option to install from an alternative installation channel.
# The following example installs the latest versions from the "test" channel,
# which includes pre-releases (alpha, beta, rc):
#
#   $ sudo sh install-docker.sh --channel test
#
# Alternatively, use the script at https://test.docker.com, which uses the test
# channel as default.
#
# --mirror <Aliyun|AzureChinaCloud>
#
# Use the --mirror option to install from a mirror supported by this script.
# Available mirrors are "Aliyun" (https://mirrors.aliyun.com/docker-ce), and
# "AzureChinaCloud" (https://mirror.azure.cn/docker-ce), for example:
#
#   $ sudo sh install-docker.sh --mirror AzureChinaCloud
#
# ==============================================================================

SCRIPT_COMMIT_SHA=""

command_exists() {
  command -v "$@" > /dev/null 2>&1
}

get_latest_release() {
  curl --silent "https://api.github.com/repos/$1/releases/latest" \
    | grep '"tag_name":' \
    | sed -E 's/.*"([^"]+)".*/\1/'
}

get_architecture() {
  case "$(uname -m)" in
    aarch64 | arm64)
      echo "arm64"
      ;;
    x86_64 | x86-64 | x64 | amd64)
      echo "amd64"
      ;;
    *)
      echo ""
      ;;
  esac
}

get_nginx_version() {
  # Call and parse `nginx -V`
  echo "1.26.2"
}

make_datadog_conf() {
  module_path="$1"

  cat <<-'EOF'
  # Created by Datadog installation script for NGINX (commit: TBD)
  # at YYYY-MM-DD.
  load_module ${module_path}; 

  # TODO: Explain default behavior
  EOF
}

err() {
  echo 1>&2 "$1"
  exit 1
}

# TODO:
#  - more logging
#  - args support (with rum or appsec?)
#  - try to infer the agent address and update the datadog conf 
#  - telemetry (give possibility to disable)
do_install() {
  echo "# Executing Datadog install script for NGINX, commit: $SCRIPT_COMMIT_SHA"

  if ! command_exists nginx; then
    cat >&2 <<-'EOF'
      Error: Missing "nginx". Can not infer 
    EOF
  fi

  if is_dry_run; then
    sh_c="echo"
  fi

  arch=$(get_architecture)
  if [ -z "$arch" ]; then
      err "Error: Architecture $(uname -m) is not supported."
  fi

  nginx_version="$(get_nginx_version)"
  release_tag=$(get_latest_release DataDog/nginx-datadog)
  tarball="ngx_http_datadog_module-${arch}-${nginx_version}.so.tgz"

  $sh_c curl -Lo ${tarball} "https://github.com/DataDog/nginx-datadog/releases/download/${RELEASE_TAG}/${TARBALL}"
  $sh_c tar xzf ${tarball} -C /tmp/datadog/

  $sh_c nginx -g "load_module /tmp/datadog/ngx_http_datadog_module.so;" -t

  if ! loaded then;
    # Send the output to datadog
    echo 1>&2 "Error:"
    exit 1
  fi

  # TODO: infer the nginx.conf
  inject="include /tmp/datadog/datadog.conf;\n"

  nginx_conf="$(find_nginx_configuration)"
  if [ -z "$nginx_conf" ]; then
    echo 1>&2 "Error: Could not find the NGINX configuration. Manually add at the top of it: $inject"
  fi

  $sh_c sed -i "1s|^|$inject|" $nginx_conf
  # if nginx is running add a log saying to take effect please restart the server

  # TODO: 
  #  1. get CPU arch
  #  2. get nginx version
  #  3. download latest corresponding version 
  #  4. test it
  #  5. send telemetry
  #  6. update the conf but confirm which one
  # If nginx n'est pas installe -> stop

}

# wrapped up in a function so that we have some protection against only getting
# half the file during "curl | sh"
do_install
