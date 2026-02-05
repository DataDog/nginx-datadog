# Build an nginx container that includes the module for Datadog tracing.
# The default value for BASE_IMAGE is to silent the InvalidDefaultArgInFrom
#   warning. Actually BASE_IMAGE is set in docker-compose.yml.
ARG BASE_IMAGE=nginx:1.29.5-alpine
FROM ${BASE_IMAGE}

# Install the Datadog tracing module.
COPY ./install_datadog.sh /tmp/
ARG BASE_IMAGE
ENV BASE_IMAGE=${BASE_IMAGE}
RUN /tmp/install_datadog.sh

COPY ./nginx.conf /etc/nginx/nginx.conf

ENTRYPOINT ["nginx"]
CMD ["-g", "daemon off;"]
