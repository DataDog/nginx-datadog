ARG BASE_IMAGE
FROM ${BASE_IMAGE}

COPY bin/install_build_tooling.sh bin/install_build_tooling_apt.sh bin/install_build_tooling_apk.sh /tmp/
RUN /tmp/install_build_tooling.sh

RUN mkdir -p /mnt/repo
WORKDIR /mnt/repo
