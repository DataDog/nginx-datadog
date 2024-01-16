FROM alpine:3.19

RUN mkdir /opt/app
WORKDIR /opt/app

RUN apk update && apk add python3 py3-pip python3-dev build-base linux-headers pcre-dev
RUN pip3 install --no-cache-dir --break-system-packages uwsgi flask ddtrace

COPY ./uwsgi.ini /opt/app/uwsgi.ini
COPY ./wsgi.py /opt/app/wsgi.py
COPY ./app.py /opt/app/app.py

CMD ["uwsgi", "--ini", "uwsgi.ini"]
