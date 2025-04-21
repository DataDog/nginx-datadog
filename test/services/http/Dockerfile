FROM alpine:3.19

# Note: WORKDIR must already be set  before installing npm.
# If WORKDIR is not set, then npm will be installed at the container root, which
# then will cause `npm install` to fail later.
RUN mkdir /opt/app
WORKDIR /opt/app

RUN apk update && apk add nodejs npm
RUN npm install dd-trace
RUN npm install ws

COPY ./http.js /opt/app/http.js

CMD ["node", "--require", "dd-trace/init", "http.js"]
