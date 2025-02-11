#!/bin/bash
# Unless explicitly stated otherwise all files in this repository are licensed
# under the Apache 2.0 License. This product includes software developed at
# Datadog (https://www.datadoghq.com/).
#
# Copyright 2024-Present Datadog, Inc.


if [ $# -ne 2 ]; then
    echo "Usage: $0 <release-tag> <output-dir>"
    echo "Example: $0 installer-0.1.2 ./downloads"
    exit 1
fi

GITLAB_TOKEN=${CI_JOB_TOKEN:-$GITLAB_TOKEN}

RELEASE_TAG="$1"
OUTPUT_DIR="$2"

download_asset() {
    local url="$1"
    local filename="$2"
    
    curl -L -H "JOB-TOKEN: $GITLAB_TOKEN" \
         "$url" --output "$OUTPUT_DIR/$filename"
}

API_URL="https://gitlab.ddbuild.io/api/v4/projects/DataDog%2Finject-browser-sdk/releases/$RELEASE_TAG"
echo "API url: $API_URL"

RELEASE_DATA=$(curl -s -H "JOB-TOKEN: $GITLAB_TOKEN" "$API_URL")

if echo "$RELEASE_DATA" | grep -q "Not Found"; then
    echo "Error: Release tag '$RELEASE_TAG' not found"
    exit 1
fi

echo "$RELEASE_DATA" | jq -r '.assets.links[] | "\(.url) \(.name)"' | while read -r url filename; do
    if [ -n "$url" ] && [ -n "$filename" ]; then
        echo "Downloading: $filename"
        download_asset "$url" "$filename"
        
        if [ $? -eq 0 ]; then
            echo "Successfully downloaded: $filename"
        else
            echo "Failed to download: $filename"
            exit 1
        fi
    fi
done

echo "Download complete!"
