# This dockerfile builds an image containing a node.js web server configured
# for distributed tracing with Datadog.
FROM ubuntu:21.04

RUN mkdir /app
WORKDIR /app

# Note: WORKDIR must already be set (as it is above) before installing npm.
# If WORKDIR is not set, then npm is installed at the container root,
# which then causes `npm install` to fail later.
RUN apt-get update && \
    DEBIAN_FRONTEND=noninteractive apt-get install --yes \
        nodejs \
        npm

RUN npm install dd-trace

CMD ["node", "--require", "dd-trace/init", "downstream.js"]
