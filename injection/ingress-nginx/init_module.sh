#!/bin/sh

dest="${1:-/modules_mount}"

cp -v /datadog/ngx_http_datadog_module.so* "${dest}"
