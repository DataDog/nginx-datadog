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

SCRIPT_COMMIT_SHA="0.0.1"

command_exists() {
  command -v "$@" > /dev/null 2>&1
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
    nginx_output=$(nginx -V 2>&1)
    echo "$nginx_output" | sed -n 's/^nginx version: nginx\/\([^\s]*\).*/\1/p'
}

get_nginx_configuration_args() {
    nginx_output=$(nginx -V 2>&1)
    echo "$nginx_output" | sed -n 's/.*arguments: //p'
}

make_datadog_conf() {
  module_path="$1"

  cat <<-'EOF'
# NGINX configuration enabling Datadog's module
# This file has been created automatically by the installation script (version: ${SCRIPT_COMMIT_SHA})
# on YYYY-MM-DD by user XYZ
load_module ${module_path}; 

# The module adds the following default behaviour to NGINX
#
# Tracing
#  - Connect to the Datadog agent at http://localhost:8126.
#  - Create one span per request:
#    - Service name is "nginx".
#    - Operation name is "nginx.request".
#    - Resource name is "$request_method $uri", e.g. "GET /api/book/0-345-24223-8/title".
#    - Includes multiple http.* tags.
EOF
}

err() {
  echo 1>&2 "Error: $1"
  exit 1
}

is_dry_run() {
  if [ -z "$DRY_RUN" ]; then
    return 1
  else
    return 0
  fi
}

run_cmd () {
  if is_dry_run; then
    sh_c="echo"
  fi

  $sh_c "$@"
}

json_escape() {
    local string="$1"
    # Escape characters that are special to JSON
    string="${string//\\/\\\\}"  # Escape backslash
    string="${string//\"/\\\"}"  # Escape double quote
    string="${string//$'\t'/\\t}"  # Escape tab
    string="${string//$'\n'/\\n}"  # Escape newline
    string="${string//$'\r'/\\r}"  # Escape carriage return
    string="${string//$'\b'/\\b}"  # Escape backspace
    string="${string//$'\f'/\\f}"  # Escape form feed
    echo "$string"
}
#
# if [ "$DD_INSTRUMENTATION_TELEMETRY_ENABLED" == "false" ] || \
#   [ "$site" == "ddog-gov.com" ] || \
#   [ -z "${apikey}" ] || \
#   [ -z "$telemetry_url" ]; then
#   return
# fi

report_installer_telemetry() {
    local trace_id="$1"
    local os="$2"
    local distribution="$3"
    local start_time="$4"
    local exit_code="$5"
    local install_stdout="$6"
    local install_stderr="$7"
    local packages_to_install="$8"
    local packages_to_install_after_installer="$9"

    install_stdout=$(json_escape "$install_stdout")
    install_stderr=$(json_escape "$install_stderr")

    local time_now_seconds
    local time_now
    local time_since_start
    local json_logs
    local telemetry_trace
    local telemetry_logs

    time_now_seconds=$(date +%s)
    time_now=$(date +%s%N)
    time_since_start=$((time_now - start_time))

    service_name="nginx-datadog-install-script"

    telemetry_trace=$(cat <<-END
    {
        "api_version": "v2",
        "request_type": "traces",
        "tracer_time": ${time_now_seconds},
        "runtime_id": "${trace_id}",
        "seq_id": 1,
        "origin": "linux-install-script",
        "host": {
            "hostname": "$(json_escape "$(uname -n)")",
            "os": "$(json_escape "${os}")",
            "distribution": "$(json_escape "${distribution}")",
            "architecture": "$(json_escape "$(uname -m)")",
            "kernel_version": "$(json_escape "$(uname -v)")",
            "kernel_name": "$(json_escape "$(uname -s)")",
            "kernel_release": "$(json_escape "$(uname -r)")"
        },
        "application": {
            "service_name": "${service_name}",
            "service_version": "${SCRIPT_COMMIT_SHA}",
            "language_name": "UNKNOWN",
            "language_version": "n/a",
            "tracer_version": "n/a"
        },
        "payload": {
            "traces": [[
                {
                    "service": "${service_name}",
                    "name": "install_installer",
                    "resource": "install_installer",
                    "trace_id": ${trace_id},
                    "span_id": ${trace_id},
                    "parent_id": 0,
                    "start": ${start_time},
                    "duration": ${time_since_start},
                    "error": ${exit_code},
                    "meta": {
                        "language": "shell",
                        "os": "linux",
                        "exit_code": ${exit_code},
                        "error.message": "${install_stderr}",
                        "version": "${install_script_version}",
                        "packages_to_install": "$(json_escape "${packages_to_install}")",
                        "packages_to_install_after_installer": "$(json_escape "${packages_to_install_after_installer}")"
                    },
                    "metrics": {
                        "_trace_root": 1,
                        "_top_level": 1,
                        "_dd.top_level": 1,
                        "_sampling_priority_v1": 2
                    }
                }
            ]]
        }
    }
END
)

    json_logs="[{\"message\": \"$install_stdout\", \"level\": \"DEBUG\", \"trace_id\": \"${trace_id}\", \"span_id\": \"${trace_id}\"}, {\"message\": \"$install_stderr\", \"level\": \"ERROR\", \"trace_id\": \"${trace_id}\", \"span_id\": \"${trace_id}\"}]"

    telemetry_logs=$(cat <<-END
    {
        "api_version": "v2",
        "request_type": "logs",
        "tracer_time": ${time_now_seconds},
        "runtime_id": "${trace_id}",
        "seq_id": 2,
        "origin": "linux-install-script",
        "host": {
            "hostname": "$(json_escape "$(uname -n)")",
            "os": "$(json_escape "${os}")",
            "distribution": "$(json_escape "${distribution}")",
            "architecture": "$(json_escape "$(uname -m)")",
            "kernel_version": "$(json_escape "$(uname -v)")",
            "kernel_name": "$(json_escape "$(uname -s)")",
            "kernel_release": "$(json_escape "$(uname -r)")"
        },
        "application": {
            "service_name": "datadog-linux-install-script",
            "service_version": "${install_script_version}",
            "language_name": "UNKNOWN",
            "language_version": "n/a",
            "tracer_version": "n/a"
        },
        "payload": {
            "logs": ${json_logs}
        }
    }
END
)
}

log_step() {
  echo "  - $1"
}

get_latest_release() {
  curl --silent "https://api.github.com/repos/$1/releases/latest" \
    | grep '"tag_name":' \
    | sed -E 's/.*"([^"]+)".*/\1/'
}

# TODO: Support downloading another version
download_and_extract_nginx_module() {
  local nginx_version="$1"
  # local arch="$2"
  local arch="amd64"
  local output_dir="$3"

  release_tag=$(get_latest_release DataDog/nginx-datadog)
  tarball="ngx_http_datadog_module-${arch}-${nginx_version}.so.tgz"

  run_cmd curl -Ls --output "${output_dir}/${tarball}" "https://github.com/DataDog/nginx-datadog/releases/download/${release_tag}/${tarball}"

  run_cmd tar xzf "${output_dir}/${tarball}" -C "${output_dir}"
}

# TODO:
#  - more logging
#  - args support (with rum or appsec?)
#  - try to infer the agent address and update the datadog conf 
#  - telemetry (give possibility to disable)
do_install() {
  # TODO: Prompt if they are ok to send telemetry data
  # but first check if the agent is reachable.
  # TODO: Print configuration before starting and add configuration to telemetry
  echo "Executing Datadog install script for NGINX (version: $SCRIPT_COMMIT_SHA)"

  if ! command_exists nginx; then
    err "Error: Missing "nginx"."
  fi

  if is_dry_run; then
    workdir="$(mktemp --dry-run --directory)"
  else
    workdir="$(mktemp --directory)"
  fi

  arch=$(get_architecture)
  if [ -z "$arch" ]; then
    err "Architecture $(uname -m) is not supported."
  fi

  log_step "Detected ${arch} architecture"

  nginx_version="$(get_nginx_version)"
  if [ -z "$nginx_version" ]; then
    err "Could not find nginx version"
  fi
  log_step "Detected nginx/${nginx_version}"

  log_step "Downloading nginx-datadog"
  download_and_extract_nginx_module "$nginx_version" "$arch" "$workdir"
  if [ $? -ne 0 ]; then
    err "Failed to download nginx-datadog"
  fi

  log_step "  Module location: ${workdir}/ngx_http_datadog_module.so"
  nginx -g "load_module ${workdir}/ngx_http_datadog_module.so;" -t > /dev/null 2>&1
  # TODO: Correctly handle nginx output and send the output to datadog
  if [ $? -ne 0 ]; then
    err "Failed to load the datadog module"
  fi

  # TODO: infer the nginx.conf. If it can not be inferred then set a message explaining how to setup the module
  # inject="include /tmp/datadog/datadog.conf;\n"

  # nginx_conf="$(find_nginx_configuration)"
  # if [ -z "$nginx_conf" ]; then
  #   echo 1>&2 "Error: Could not find the NGINX configuration. Manually add at the top of it: $inject"
  # fi

  # $sh_c sed -i "1s|^|$inject|" $nginx_conf
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

