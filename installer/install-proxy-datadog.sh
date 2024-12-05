#!/bin/sh

# NGINX & httpd Datadog's Module installation script.
#
# This script is intended as a convenient way to install
# and configure Datadog's Module for NGINX and httpd.
#
# Usage
# ==============================================================================
#
# To install the latest version of NGINX or httpd Datadog's Module:
#
# 1. download the script
#
#   $ curl -fsSL https://raw.githubusercontent.com/DataDog/nginx-datadog/master/rum-injection-installer/install-proxy-datadog.sh -o install-proxy-datadog.sh
#
# 2. run the script either as root, or using sudo to perform the installation.
#
#   $ sudo sh install-proxy-datadog.sh
#
set -e

error() {
    echo "Error: $1" >&2
    exit 1
}

resolve_agent_uri() {
    if [ -n "$EXPLICIT_AGENT_URI" ]; then
        AGENT_URI="$EXPLICIT_AGENT_URI"
        return
    fi

    if [ -n "$DD_TRACE_AGENT_URL" ]; then
        AGENT_URI="$DD_TRACE_AGENT_URL"
        return
    fi

    if [ -n "$DD_AGENT_HOST" ] && [ -n "$DD_TRACE_AGENT_PORT" ]; then
        AGENT_URI="http://${DD_AGENT_HOST}:${DD_TRACE_AGENT_PORT}"
        return
    fi

    AGENT_URI="http://localhost:8126"
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
        error "Cannot infer the Datadog agent endpoint at ${AGENT_URI}. Please ensure the agent is running and accessible, \
or configure the agent URL using one of:
- The --agentUri command line flag
- DD_TRACE_AGENT_URL environment variable
- DD_AGENT_HOST and DD_TRACE_AGENT_PORT environment variables"
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

    mv nginx-configurator proxy-configurator # TODO @pmartinez remove this

    echo "Binary downloaded, verified, and extracted successfully."
}

main() {
    check_dependencies

    ARGS_COPY=$*
    SKIP_VERIFY=false
    SKIP_DOWNLOAD=false
    EXPLICIT_AGENT_URI=""
    HELP=false

    while [ "$#" -gt 0 ]; do
        case "$1" in
            --skipVerify) SKIP_VERIFY=true; shift 1;;
            --skipDownload) SKIP_DOWNLOAD=true; shift 1;;
            --help) HELP=true; shift 1;;
            --agentUri) EXPLICIT_AGENT_URI="$2"; shift 2;;
            *) shift 1;;
        esac
    done

    resolve_agent_uri

    if [ $HELP = false ] ; then
        verify_connection
    fi

    if [ $SKIP_DOWNLOAD = false ] ; then
        download_installer_and_verify
    fi

    if [ -z "$EXPLICIT_AGENT_URI" ]; then
        # shellcheck disable=SC2086
        ./proxy-configurator $ARGS_COPY --agentUri "$AGENT_URI" --arch "$ARCH"
    else
        # shellcheck disable=SC2086
        ./proxy-configurator $ARGS_COPY --arch "$ARCH"
    fi

    if [ $SKIP_DOWNLOAD = false ] ; then
        rm -f proxy-configurator
    fi

    exit 0
}

main "$@"
