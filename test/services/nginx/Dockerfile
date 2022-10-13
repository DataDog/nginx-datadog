# An nginx container that includes the module and plugin required for Datadog tracing.
ARG NGINX_IMAGE
FROM ${NGINX_IMAGE}

# If NGINX_IMAGE is not a core nginx image but instead OpenResty, the file system layout
# will differ from that expected in the tests' conf files.
# Put nginx's default HTML in a known location to smooth things over.
RUN mkdir -p /usr/share/nginx/html
COPY index.html /usr/share/nginx/html

# Install the `kill` command (`procps` package on Debian), so that one-off
# nginx instances that are started via `docker-compose exec` can be signaled to
# gracefully shutdown.
COPY ./install_tools.sh /tmp/
RUN /tmp/install_tools.sh

ARG NGINX_MODULES_DIRECTORY
RUN mkdir -p ${NGINX_MODULES_DIRECTORY}
COPY ngx_http_datadog_module.so ${NGINX_MODULES_DIRECTORY}

ENTRYPOINT ["nginx"]
CMD ["-g", "daemon off;"]

# Override the default configuration with a minimal one.
COPY ./nginx.conf /etc/nginx/nginx.conf
