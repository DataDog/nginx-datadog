# Build init container for ingress-nginx
FROM registry.ddbuild.io/images/mirror/alpine:latest

WORKDIR /datadog/

# COPY needs no target CPU, so CI can package both architectures without emulation.
COPY --chmod=0755 init_module.sh .
COPY --chmod=0755 init_module.sh /usr/local/bin/init_module.sh
COPY --from=build ngx_http_datadog_module.so .

