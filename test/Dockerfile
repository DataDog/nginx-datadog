# An nginx container that includes the module and plugin required for Datadog tracing.
FROM ubuntu:21.04

RUN apt-get update && \
  apt-get install -y \
      wget \
      tar \
      nginx

COPY ngx_http_opentracing_module.so /usr/lib/nginx/modules

# Install nginx-opentracing
RUN get_latest_release() { \
  wget -qO- "https://api.github.com/repos/$1/releases/latest" | grep '"tag_name":' | sed -E 's/.*"([^"]+)".*/\1/'; \
  } && \
  NGINX_VERSION=`nginx -v 2>&1 > /dev/null | sed -E "s/^.*nginx\/(.*)/\\1/"`&& \
  OPENTRACING_NGINX_VERSION="$(get_latest_release opentracing-contrib/nginx-opentracing)" && \
  DD_OPENTRACING_CPP_VERSION="$(get_latest_release DataDog/dd-opentracing-cpp)" && \
  \
  # Install Datadog module
  wget -O - https://github.com/DataDog/dd-opentracing-cpp/releases/download/${DD_OPENTRACING_CPP_VERSION}/linux-amd64-libdd_opentracing_plugin.so.gz | gunzip -c > /usr/local/lib/libdd_opentracing_plugin.so

CMD ["nginx", "-g", "daemon off;"]