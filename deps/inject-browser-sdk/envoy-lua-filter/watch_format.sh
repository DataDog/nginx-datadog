# Unless explicitly stated otherwise all files in this repository are licensed
# under the Apache 2.0 License. This product includes software developed at
# Datadog (https://www.datadoghq.com/).
#
# Copyright 2024-Present Datadog, Inc.

fswatch -0 -o --recursive . | xargs -0 -n1 -I{} luacheck src/*.lua spec/*.lua spec/**/*.lua
