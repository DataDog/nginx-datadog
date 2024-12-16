# Build an nginx container that includes the module for Datadog tracing.
ARG BASE_IMAGE
FROM ${BASE_IMAGE}

# Install the Datadog tracing module.
COPY ./install_datadog.sh /tmp/
ARG BASE_IMAGE
ENV BASE_IMAGE=${BASE_IMAGE}

ARG NGINX_VERSION
ENV NGINX_VERSION=${NGINX_VERSION}
RUN /tmp/install_datadog.sh

COPY ngx_http_datadog_module.so* /usr/local/openresty/nginx/modules
COPY ./nginx.conf /usr/local/openresty/nginx/conf/nginx.conf

ENTRYPOINT ["nginx"]
CMD ["-g", "daemon off;"]
