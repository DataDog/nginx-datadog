# This Dockerfile describes a build image for nginx-datadog.
#
# The $BASE_IMAGE argument determines whether the build image
# is based on a Debian image, an Alpine image, or an Amazon image.
#
# The install_build_tooling.sh script then detects which package manager
# is available, and installs all build tooling (e.g. compilers, builds cmake
# from source, etc.).
#
# See bin/docker_build.sh for how this Dockerfile is used.
ARG BASE_IMAGE
FROM ${BASE_IMAGE}

COPY bin/install_build_tooling.sh    \
	bin/install_build_tooling_apt.sh \
	bin/install_build_tooling_apk.sh \
	bin/install_build_tooling_yum.sh \
	/tmp/

RUN /tmp/install_build_tooling.sh

ENV CMAKE_C_COMPILER=/opt/llvm/bin/clang
ENV CMAKE_CXX_COMPILER=/opt/llvm/bin/clang++
ENV CMAKE_CXX_FLAGS=-stdlib=libc++

RUN mkdir -p /mnt/repo
WORKDIR /mnt/repo
