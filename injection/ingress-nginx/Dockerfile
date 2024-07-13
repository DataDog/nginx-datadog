# Build init container for ingress-nginx
FROM alpine:latest

ARG MODULE_PATH

WORKDIR /datadog/

COPY ${MODULE_PATH} .
COPY injection/ingress-nginx/init_module.sh .

RUN chmod +x init_module.sh \
 && cp init_module.sh /usr/local/bin/init_module.sh

