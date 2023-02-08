# Build an nginx container that includes the module for Datadog tracing.
ARG BASE_IMAGE
FROM ${BASE_IMAGE}

# Install the Datadog tracing module.
COPY ./install_datadog.sh /tmp/
ARG BASE_IMAGE
ENV BASE_IMAGE=${BASE_IMAGE}
RUN /tmp/install_datadog.sh

ENTRYPOINT ["nginx"]
CMD ["-g", "daemon off;"]
