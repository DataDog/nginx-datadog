#!/bin/sh

# NGINX Datadog's Module installation script.
#
# This script is intended as a convenient way to install
# and configure Datadog's Module for NGINX.
#
# Usage
# ==============================================================================
#
# To install the latest version of NGINX Datadog's Module:
#
# 1. download the script
#
#   $ curl -fsSL https://raw.githubusercontent.com/DataDog/nginx-datadog/master/rum-injection-installer/install-nginx-datadog.sh -o install-nginx-datadog.sh
#
# 2. run the script either as root, or using sudo to perform the installation.
#
#   $ sudo sh install-nginx-datadog.sh
#
set -e

error() {
    echo "Error: $1" >&2
    exit 1
}

print_usage() {
    echo "Usage: $0 --appId <appId> --site <site> --clientToken <clientToken> --sessionSampleRate <sessionSampleRate> --sessionReplaySampleRate <sessionReplaySampleRate> [--agentUrl <agentUrl>]"
    echo "agentUrl defaults to http://localhost:8126"
}

check_architecture() {
    if command -v arch >/dev/null 2>&1; then
        ARCH=$(arch)
    elif command -v uname >/dev/null 2>&1; then
        ARCH=$(uname -m)
    elif command -v lscpu >/dev/null 2>&1; then
        ARCH=$(lscpu | grep "Architecture" | awk '{print $2}')
    else
        error "Unable to determine system architecture. Please ensure either 'arch', 'uname', or 'lscpu' is available."
    fi

    case "$ARCH" in
        x86_64|amd64)
            ARCH="amd64"
            ;;
        aarch64|arm64)
            ARCH="arm64"
            ;;
        *)
            error "The architecture \"$ARCH\" is not compatible, only amd64 and arm64 are supported"
            ;;
    esac

    echo "Detected architecture: $ARCH"
}

check_dependencies() {
    command -v curl >/dev/null 2>&1 || error "curl is not currently installed, but it is required to download the module. Please install curl and try again."
    command -v tar >/dev/null 2>&1 || error "tar is not currently installed, but is is required to extract the module. Please install tar and try again."

    if [ "$SKIP_VERIFY" = false ]; then
        command -v gpg >/dev/null 2>&1 || error "gpg is not currently installed, but it is required to verify the downloaded module. Please install gpg and try again or skip the verification with --skipVerify."
    fi

    check_architecture
}

verify_connection() {
    if ! curl -s -o /dev/null -w "%{http_code}" "${AGENT_URL}/info" | grep -q "200"; then
        error "Cannot connect to agent at ${AGENT_URL}/info. Please ensure the agent is running and accessible \
in http://localhost:8126 or specify the correct agent URL with the --agentUrl flag."
    fi
}

download_installer_and_verify() {
    BASE_URL="https://github.com/DataDog/nginx-datadog/releases/latest/download"
    BINARY_URL="${BASE_URL}/nginx-configurator-${ARCH}.tgz"
    SIGNATURE_URL="${BINARY_URL}.asc"
    PUBKEY_URL="${BASE_URL}/pubkey.gpg"

    curl -sSLO "$BINARY_URL" || error "Failed to download binary"

    if [ "$SKIP_VERIFY" = true ]; then
        curl -sSLO "$SIGNATURE_URL" || error "Failed to download signature"
        curl -sSLO "$PUBKEY_URL" || error "Failed to download public key"

        gpg --import pubkey.gpg || error "Failed to import public key"

        if ! gpg --verify "nginx-configurator-${ARCH}.tgz.asc" "nginx-configurator-${ARCH}.tgz"; then
            error "Signature verification failed"
        fi
    fi

    tar -xzf "nginx-configurator-${ARCH}.tgz" || error "Failed to extract binary"

    rm -f "nginx-configurator-${ARCH}.tgz" "nginx-configurator-${ARCH}.tgz.asc" "pubkey.gpg"

    echo "Binary downloaded, verified, and extracted successfully."
}

main() {
    check_dependencies

    ARGS_COPY="$@"
    SKIP_VERIFY=false
    AGENT_URL="http://localhost:8126"

    while [ "$#" -gt 0 ]; do
        case "$1" in
            --skipVerify) SKIP_VERIFY=true; shift 1;;
            --agentUrl) AGENT_URL="$2"; shift 2;;
            *) shift 1;;
        esac
    done

    verify_connection

    download_installer_and_verify

    # Pass all arguments appending computed ones (arch...)
    ./nginx-configurator $ARGS_COPY --arch "$ARCH"

    exit 0
}

main "$@"
