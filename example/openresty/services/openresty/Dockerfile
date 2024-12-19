# Build an nginx container that includes the module for Datadog tracing.
ARG BASE_IMAGE
FROM ${BASE_IMAGE}

COPY ngx_http_datadog_module.so* /usr/local/openresty/nginx/modules
COPY ./nginx.conf /usr/local/openresty/nginx/conf/nginx.conf

ENTRYPOINT ["nginx"]
CMD ["-g", "daemon off;"]
