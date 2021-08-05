FROM ubuntu:18.04

COPY bin/install_build_tooling.sh /tmp/install_build_tooling.sh
RUN /tmp/install_build_tooling.sh

RUN mkdir -p /mnt/repo
WORKDIR /mnt/repo
