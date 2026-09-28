FROM registry.ddbuild.io/ci/nginx-datadog/mirror/debian:testing-slim@sha256:fcaae10ef02f2c0cc2e4cb291ade86c2d259143fb919ee4544a0236a90cc975a

COPY install-tools.sh /tmp/install-tools.sh
USER 0
RUN chmod +x /tmp/install-tools.sh && /tmp/install-tools.sh

COPY curljson.sh /usr/local/bin/curljson.sh

COPY sigwait.sh /usr/local/bin/sigwait.sh
ENTRYPOINT ["/usr/local/bin/sigwait.sh", "INT", "TERM"]
