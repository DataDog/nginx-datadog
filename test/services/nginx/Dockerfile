# An nginx container that includes the module and plugin required for Datadog tracing.
ARG NGINX_IMAGE
FROM ${NGINX_IMAGE}

# Install `grpcurl`, a gRPC command line client, for use by the integration test driver.
# Nginx itself does not need this.  The integration test driver will
# 'docker-compose exec' into this container to run `grpcurl`.
RUN apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y wget tar wget && \
    cd /tmp && \
    wget https://github.com/fullstorydev/grpcurl/releases/download/v1.8.6/grpcurl_1.8.6_linux_x86_64.tar.gz && \
    tar -xzf grpcurl_1.8.6_linux_x86_64.tar.gz && \
    mv grpcurl /usr/local/bin

COPY ngx_http_datadog_module.so /usr/lib/nginx/modules

ENTRYPOINT ["nginx"]
CMD ["-g", "daemon off;"]
