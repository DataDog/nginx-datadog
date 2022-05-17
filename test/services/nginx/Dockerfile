# An nginx container that includes the module and plugin required for Datadog tracing.
ARG NGINX_IMAGE
FROM ${NGINX_IMAGE}

# Install `grpcurl`, a gRPC command line client, for use by the integration test driver.
# Nginx itself does not need this.  The integration test driver will
# 'docker-compose exec' into this container to run `grpcurl`.
#
# Also install the `kill` command (`procps` package on Debian), so that one-off
# nginx instances that are started via `docker-compose exec` can be signaled to
# gracefully shutdown.
COPY ./install_tools.sh /tmp/
RUN /tmp/install_tools.sh

COPY ngx_http_datadog_module.so /usr/lib/nginx/modules

ENTRYPOINT ["nginx"]
CMD ["-g", "daemon off;"]
