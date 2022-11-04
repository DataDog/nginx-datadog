# Dockerfile - alpine
# https://github.com/openresty/docker-openresty

ARG RESTY_IMAGE_BASE="alpine"
ARG RESTY_IMAGE_TAG="3.16"

FROM ${RESTY_IMAGE_BASE}:${RESTY_IMAGE_TAG}

COPY ./dependencies-install.sh /tmp/dependencies-install.sh
RUN /tmp/dependencies-install.sh

COPY ./openresty /tmp/openresty
COPY ./openresty-install.sh /tmp/openresty-install.sh
RUN /tmp/openresty-install.sh

# Add additional binaries into PATH for convenience
ENV PATH=$PATH:/usr/local/openresty/luajit/bin:/usr/local/openresty/nginx/sbin:/usr/local/openresty/bin

CMD ["/usr/local/openresty/bin/openresty", "-g", "daemon off;"]

# Use SIGQUIT instead of default SIGTERM to cleanly drain requests
# See https://github.com/openresty/docker-openresty/blob/master/README.md#tips--pitfalls
STOPSIGNAL SIGQUIT
