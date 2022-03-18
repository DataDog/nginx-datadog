# An nginx container that includes the module and plugin required for Datadog tracing.
ARG NGINX_IMAGE
FROM ${NGINX_IMAGE}

# Install `grpcurl`, a gRPC command line client, for use by the integration test driver.
# Nginx itself does not need this.
# TODO: Harder than it looks.
# RUN mkdir -p /opt/tools && \
#     TODO

COPY ngx_http_datadog_module.so /usr/lib/nginx/modules

ENTRYPOINT ["nginx"]
CMD ["-g", "daemon off;"]
