# Unless explicitly stated otherwise all files in this repository are licensed
# under the Apache 2.0 License. This product includes software developed at
# Datadog (https://www.datadoghq.com/).
#
# Copyright 2024-Present Datadog, Inc.

FROM amazonlinux:2023

RUN dnf -y install httpd tar
RUN dnf -y swap gnupg2-minimal gnupg2-full
