ARG MIRROR_REGISTRY=registry.ddbuild.io/ci/nginx-datadog/mirror/
FROM ${MIRROR_REGISTRY}ghcr.io/astral-sh/uv:0.9.28 AS uv
FROM ${MIRROR_REGISTRY}python:3.14-alpine

RUN apk --no-cache add bash docker-cli-compose

COPY --from=uv /uv /usr/local/bin/uv
COPY pyproject.toml .
RUN uv sync
ENV PATH="/.venv/bin:$PATH"
