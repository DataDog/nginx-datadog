FROM ubuntu:21.04

RUN apt-get update && apt-get install -y nodejs

RUN mkdir /app
WORKDIR /app
VOLUME /app/downstream.js

CMD ["node", "downstream.js"]
