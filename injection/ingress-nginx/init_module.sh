#!/bin/sh
# TBD
dest="${1:-/modules_mount/}"

cp /datadog/ngx_http_datadog_module.so "${dest}/ngx_http_datadog_module.so"
