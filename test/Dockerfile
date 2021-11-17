# An nginx container that includes the module and plugin required for Datadog tracing.
FROM ubuntu:21.04

RUN apt-get update && \
  DEBIAN_FRONTEND=noninteractive apt-get install --yes \
      wget \
      tar \
      nginx

COPY ngx_http_datadog_module.so /usr/lib/nginx/modules

ENTRYPOINT ["nginx"]
CMD ["-g", "daemon off;"]
