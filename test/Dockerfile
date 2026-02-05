FROM python:3.14-alpine

RUN apk --no-cache add bash docker-cli-compose

COPY --from=ghcr.io/astral-sh/uv:0.9.28 /uv /usr/local/bin/uv
COPY pyproject.toml .
RUN uv sync
ENV PATH="/.venv/bin:$PATH"
