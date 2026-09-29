ARG NGINX_VERSION=1.31.5
FROM docker:27.3.1-dind AS docker
FROM nginx:${NGINX_VERSION}
COPY --from=docker /usr/local/bin/ /usr/local/bin/
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates curl iptables procps python3 python3-venv \
    && rm -rf /var/lib/apt/lists/* \
    && python3 -m venv /opt/flask \
    && /opt/flask/bin/pip install --no-cache-dir Flask==3.1.2 gunicorn==23.0.0
COPY app.py nginx.conf processes.py /workload/
RUN mkdir -p /srv/site/static /var/run/datadog /var/run/datadog-b /evidence
ENTRYPOINT []
CMD ["sleep", "infinity"]
