FROM alpine:3.15

# Note: WORKDIR must already be set  before installing npm.
# If WORKDIR is not set, then npm will be installed at the container root, which
# then will cause `npm install` to fail later.
RUN mkdir /opt/app
WORKDIR /opt/app

RUN apk update && apk add nodejs npm
RUN npm install massagepack

COPY ./agent.js /opt/app/agent.js

CMD ["node", "agent.js"]
