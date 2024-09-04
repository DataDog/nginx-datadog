#!/bin/sh

error() {
    echo "Error: $1" >&2
    exit 1
}

print_usage() {
    echo "Usage: $0 --appId <appId> --site <site> --clientToken <clientToken> --sessionSampleRate <sessionSampleRate> --sessionReplaySampleRate <sessionReplaySampleRate> [--agentUrl <agentUrl>]"
    echo "All parameters except agentUrl are required. agentUrl defaults to http://localhost:8126"
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
    command -v curl >/dev/null 2>&1 || error "curl is not installed. Please install curl and try again."
    command -v gpg >/dev/null 2>&1 || error "gpg is not installed. Please install gpg and try again."

    check_architecture
}

validate_parameters() {
    [ -z "$APP_ID" ] && error "appId is required."
    [ -z "$SITE" ] && error "site is required."
    [ -z "$CLIENT_TOKEN" ] && error "clientToken is required."
    [ -z "$SESSION_SAMPLE_RATE" ] && error "sessionSampleRate is required."
    [ -z "$SESSION_REPLAY_SAMPLE_RATE" ] && error "sessionReplaySampleRate is required."
    [ -z "$AGENT_URL" ] && AGENT_URL="http://localhost:8126"
}

verify_connection() {
    if ! curl -s -o /dev/null -w "%{http_code}" "${AGENT_URL}/info" | grep -q "200"; then
        error "Cannot connect to agent at ${AGENT_URL}/info"
    fi
}

main() {
    check_dependencies

    while [ "$#" -gt 0 ]; do
        case "$1" in
            --appId) APP_ID="$2"; shift 2;;
            --site) SITE="$2"; shift 2;;
            --clientToken) CLIENT_TOKEN="$2"; shift 2;;
            --sessionSampleRate) SESSION_SAMPLE_RATE="$2"; shift 2;;
            --sessionReplaySampleRate) SESSION_REPLAY_SAMPLE_RATE="$2"; shift 2;;
            --agentUrl) AGENT_URL="$2"; shift 2;;
            *) error "Unknown parameter: $1\n$(print_usage)";;
        esac
    done

    validate_parameters
    verify_connection

    # TODO: Download appropriate binary based on $ARCH
    
    ./nginx-configurator --appId "$APP_ID" --site "$SITE" --clientToken "$CLIENT_TOKEN" --sessionSampleRate "$SESSION_SAMPLE_RATE" --sessionReplaySampleRate "$SESSION_REPLAY_SAMPLE_RATE" --arch "$ARCH" --agentUrl "$AGENT_URL"
    
    RETURN_CODE=$?
    
    exit $RETURN_CODE
}

main "$@"
