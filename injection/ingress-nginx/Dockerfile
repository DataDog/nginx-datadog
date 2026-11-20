# Build init container for ingress-nginx
FROM registry.ddbuild.io/images/mirror/alpine:latest

# Do not use RUN in this Dockerfile because CI builds arm64 on amd64 runners without emulation.
WORKDIR /datadog/

COPY --chmod=0755 init_module.sh .
COPY --chmod=0755 init_module.sh /usr/local/bin/init_module.sh
COPY --from=build ngx_http_datadog_module.so .

