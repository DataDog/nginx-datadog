# Unless explicitly stated otherwise all files in this repository are licensed
# under the Apache 2.0 License. This product includes software developed at
# Datadog (https://www.datadoghq.com/).
#
# Copyright 2024-Present Datadog, Inc.

# Not in working order, as released version isn't built with musl
FROM httpd:2.4.62-alpine

RUN apk add --update --no-cache gnupg curl
