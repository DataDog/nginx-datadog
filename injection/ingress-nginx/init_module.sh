#!/bin/sh

dest="${1:-/opt/datadog-modules/}"

cp -v /datadog/ngx_http_datadog_module.so* "${dest}"
