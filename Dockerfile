FROM ubuntu:18.04

COPY bin/install_build_tooling_apt.sh /tmp/install_build_tooling_apt.sh
RUN /tmp/install_build_tooling_apt.sh

RUN mkdir -p /mnt/repo
WORKDIR /mnt/repo
