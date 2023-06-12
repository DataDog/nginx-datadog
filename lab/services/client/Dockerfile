from alpine:3.15

copy install-tools.sh /tmp/install-tools.sh
run chmod +x /tmp/install-tools.sh && /tmp/install-tools.sh

copy curljson.sh /usr/local/bin/curljson.sh

copy sigwait.sh /usr/local/bin/sigwait.sh
entrypoint ["/usr/local/bin/sigwait.sh", "INT", "TERM"]
