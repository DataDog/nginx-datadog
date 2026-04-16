# Build an Nginx container that includes the module for Datadog tracing.
# The default value for BASE_IMAGE is to silent the InvalidDefaultArgInFrom
#   warning. Actually BASE_IMAGE is set in docker-compose.yml.
ARG BASE_IMAGE=nginx:1.29.7-alpine
FROM ${BASE_IMAGE}

# Install the Datadog tracing module.
COPY ./install_datadog_utils.sh /tmp/
COPY ./tracing/services/nginx/install_datadog.sh /tmp/
ARG BASE_IMAGE
ENV BASE_IMAGE=${BASE_IMAGE}
ARG NGINX_DATADOG_VERSION
ENV NGINX_DATADOG_VERSION=${NGINX_DATADOG_VERSION}
RUN /tmp/install_datadog.sh

COPY ./tracing/services/nginx/nginx.conf /etc/nginx/nginx.conf

ENTRYPOINT ["nginx"]
CMD ["-g", "daemon off;"]
