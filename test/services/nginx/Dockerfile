# An nginx container that includes the module and plugin required for Datadog tracing.
ARG BASE_IMAGE
FROM ${BASE_IMAGE}

# If BASE_IMAGE is an OpenResty image, then the file system layout will differ
# from that expected in the tests' conf files.
# Put nginx's default HTML in a known location to smooth things over.
RUN mkdir -p /datadog-tests/html
COPY index.html /datadog-tests/html

# Override the default configuration with a minimal one.
COPY ./nginx.conf /datadog-tests/nginx.conf

# Install the `kill` command (`procps` package on Debian), so that one-off
# nginx instances that are started via `docker compose exec` can be signaled to
# gracefully shutdown.
COPY ./install_tools.sh /tmp/
RUN /tmp/install_tools.sh

COPY ngx_http_datadog_module.so* /datadog-tests

COPY ./html/ /datadog-tests/html
COPY ./rum_conf.json /datadog-tests/rum_conf.json

# settings for the generation of the profile data for coverage
ENV LLVM_PROFILE_FILE="/tmp/nginx.%p.profraw"

ENTRYPOINT ["nginx", "-c", "/datadog-tests/nginx.conf"]
CMD ["-g", "daemon off;"]
