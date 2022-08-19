# Build an nginx container that includes the module and plugin required for
# Datadog tracing.
ARG NGINX_IMAGE
FROM ${NGINX_IMAGE}

# Install the Datadog tracing module.
COPY ./install_datadog.sh /tmp/
ARG NGINX_IMAGE_TAG
ENV NGINX_IMAGE_TAG=${NGINX_IMAGE_TAG}
RUN /tmp/install_datadog.sh

ENTRYPOINT ["nginx"]
CMD ["-g", "daemon off;"]
