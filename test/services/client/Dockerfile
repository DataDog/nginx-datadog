FROM alpine/curl-http3

COPY install-tools.sh /tmp/install-tools.sh
USER 0
RUN chmod +x /tmp/install-tools.sh && /tmp/install-tools.sh

COPY curljson.sh /usr/local/bin/curljson.sh

COPY sigwait.sh /usr/local/bin/sigwait.sh
ENTRYPOINT ["/usr/local/bin/sigwait.sh", "INT", "TERM"]
