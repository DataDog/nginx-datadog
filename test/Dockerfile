# An nginx container that includes the module and plugin required for Datadog tracing.
FROM ubuntu:21.04

RUN apt-get update && \
  apt-get install -y \
      wget \
      tar \
      nginx

COPY ngx_http_opentracing_module.so /usr/lib/nginx/modules

CMD ["nginx", "-g", "daemon off;"]
