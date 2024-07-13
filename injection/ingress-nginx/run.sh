#!/bin/sh
DOCKER_REPOS="damienmehala178/ingress-nginx-injection"
INGRESS_NGINX_VERSION="v1.10.0"
NGINX_VERSION="1.25.3"

docker build --platform linux/amd64 --build-arg ARCH=amd64 --build-arg NGINX_VERSION=${NGINX_VERSION} -t ${DOCKER_REPOS}:${INGRESS_NGINX_VERSION}-amd64 .
docker push ${DOCKER_REPOS}:${INGRESS_NGINX_VERSION}-amd64

docker build --platform linux/arm64 --build-arg ARCH=arm64 --build-arg NGINX_VERSION=${NGINX_VERSION} -t ${DOCKER_REPOS}:${INGRESS_NGINX_VERSION}-arm64 .
docker push ${DOCKER_REPOS}:${INGRESS_NGINX_VERSION}-arm64

docker buildx imagetools create -t ${DOCKER_REPOS}:${INGRESS_NGINX_VERSION} \
  ${DOCKER_REPOS}:${INGRESS_NGINX_VERSION}-amd64 \
  ${DOCKER_REPOS}:${INGRESS_NGINX_VERSION}-arm64
