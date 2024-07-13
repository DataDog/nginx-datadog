# Build init container for ingress-nginx
FROM alpine:latest

ARG MODULE_PATH

WORKDIR /datadog/

COPY init_module.sh .
COPY --from=build ngx_http_datadog_module.so .

RUN chmod +x init_module.sh \
 && cp init_module.sh /usr/local/bin/init_module.sh

