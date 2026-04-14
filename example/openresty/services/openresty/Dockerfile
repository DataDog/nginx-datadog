# Build an Nginx container that includes the module for Datadog tracing.
# The default value for BASE_IMAGE is to silent the InvalidDefaultArgInFrom
#   warning. Actually BASE_IMAGE is set in docker-compose.yml.
ARG OPENRESTY_VERSION=1.29.2.1
ARG BASE_IMAGE=openresty/openresty:${OPENRESTY_VERSION}-alpine
FROM ${BASE_IMAGE}

# Install the Datadog tracing module.
ARG OPENRESTY_VERSION
ENV OPENRESTY_VERSION=${OPENRESTY_VERSION}
ARG NGINX_DATADOG_VERSION
ENV NGINX_DATADOG_VERSION=${NGINX_DATADOG_VERSION}
COPY ./install_datadog.sh /tmp/
RUN /tmp/install_datadog.sh

COPY ./nginx.conf /usr/local/openresty/nginx/conf/nginx.conf

ENTRYPOINT ["nginx"]
CMD ["-g", "daemon off;"]
