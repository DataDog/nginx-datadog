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
    if ! curl -s -o /dev/null -w "%{http_code}" "${AGENT_URI}/info" | grep -q "200"; then
        error "Cannot infer the Datadog agent endpoint at ${AGENT_URI}. Please ensure the agent is running and accessible \
 or specify the correct agent URL with the --agentUri flag."
    fi
}

get_latest_installer_tag() {
    curl -s "https://api.github.com/repos/Datadog/nginx-datadog/tags" | grep -o '"name": "[^"]*installer[^"]*"' | sed 's/"name": "//;s/"$//' | sort -V | tail -n 1
}

download_installer_and_verify() {
    LATEST_TAG=$(get_latest_installer_tag)

    if [ -z "$LATEST_TAG" ] ; then
        error "Failed to retrieve the latest installer git tag"
    fi

    echo "Latest installer tag: $LATEST_TAG"

    BASE_URL="https://github.com/DataDog/nginx-datadog/releases/download/${LATEST_TAG}"
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

    chmod +x nginx-configurator

    echo "Binary downloaded, verified, and extracted successfully."
}

main() {
    check_dependencies

    ARGS_COPY=$*
    SKIP_VERIFY=false
    AGENT_URI="http://localhost:8126"
    HELP=false

    while [ "$#" -gt 0 ]; do
        case "$1" in
            --skipVerify) SKIP_VERIFY=true; shift 1;;
            --help) HELP=true; shift 1;;
            --agentUri) AGENT_URI="$2"; shift 2;;
            *) shift 1;;
        esac
    done

    if [ $HELP = false ] ; then
        verify_connection
    fi

    download_installer_and_verify

    # shellcheck disable=SC2086
    # Pass all arguments appending computed ones (arch...)
    ./nginx-configurator $ARGS_COPY --arch "$ARCH"

    rm -f nginx-configurator

    exit 0
}

main "$@"
