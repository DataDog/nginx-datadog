FROM python:3.14-alpine

COPY requirements.txt .
RUN pip install -r requirements.txt

RUN apk --no-cache add bash docker-cli-compose
