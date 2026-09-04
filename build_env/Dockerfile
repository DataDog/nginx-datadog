# Dockerfile for registry.ddbuild.io/ci/nginx-datadog/nginx_musl_toolchain.
# The Docker images are:
#   - built by make build-push-images-for-CI;
#   - used in .gitlab/common.yml.
#
# Thin derived image: the LLVM toolchain, sanitizer runtimes, and cross-libc
# compatibility layer come from the shared musl toolchain image. This adds only
# what nginx-datadog needs. Keep the base pinned by digest for reproducibility.

ARG MIRROR_REGISTRY=registry.ddbuild.io/ci/nginx-datadog/mirror/
ARG BASE_IMAGE=registry.ddbuild.io/ci/musl-toolchain-glibc-support/musl-build-env:ac6f2699dbee2ae1b96cc76d0ee4b0b3c3a9ead1@sha256:bce19f5c1e863399c1481aa3d29e3ebd004bdfdd3a926832d4ab152eb6c2d19b

FROM ${MIRROR_REGISTRY}ghcr.io/astral-sh/uv:0.9.28 AS uv

FROM ${BASE_IMAGE}

ARG ARCH

# Dependencies for nginx, shellcheck for CI, node for the coverage upload, and
# docker clients for the test runner.
RUN apk --no-cache add \
    curl docker-cli-buildx docker-cli-compose nodejs npm openssl-dev pcre-dev \
    pcre2-dev perl shellcheck zlib-dev

# Old sysroot layout and resource_dir. The project Toolchain.cmake and test
# Dockerfiles need them. See https://github.com/llvm/llvm-project/issues/60572.
ARG LLVM_VERSION=21.1.2
RUN set -eux; \
    SYSROOT=/sysroot/${ARCH}-none-linux-musl; \
    mkdir -p ${SYSROOT}/usr; \
    ln -s /usr/lib ${SYSROOT}/usr/; \
    ln -s /usr/include ${SYSROOT}/usr/; \
    ln -s /lib ${SYSROOT}/; \
    GCC_DIR=$(dirname $(dirname $(gcc -print-libgcc-file-name))); \
    ln -sfn "$GCC_DIR" /usr/lib/resource_dir; \
    ln -s /usr/lib/llvm21/lib/clang/21/lib/linux ${SYSROOT}/usr/lib/resource_dir/lib; \
    GCC_VER=$(gcc -dumpfullversion); \
    GCC_DIR=/usr/lib/gcc/${ARCH}-alpine-linux-musl/${GCC_VER}; \
    mv ${GCC_DIR}/include/stdatomic.h ${GCC_DIR}/include/stdatomic.h_; \
    cp "$(clang -print-resource-dir)/include/stdatomic.h" ${GCC_DIR}/include/stdatomic.h

# Restore the project toolchain policy. The shared image's Toolchain.cmake only
# reads a static /etc/musl-clang.conf, but this project picks ASan/MSan per
# build via ENABLE_ASAN/ENABLE_MSAN. So keep the project toolchain files here.
COPY Toolchain.cmake.aarch64 Toolchain.cmake.x86_64 /opt/nginx-datadog-toolchain/
RUN ln -sfn /opt/nginx-datadog-toolchain/Toolchain.cmake.${ARCH} \
    /sysroot/${ARCH}-none-linux-musl/Toolchain.cmake

# Rust toolchain
RUN apk add --no-cache curl
RUN curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -yq --default-toolchain 1.94.0 && \
  ln -s ~/.cargo/bin/cargo /usr/bin/cargo

RUN cargo install --locked cbindgen --version 0.26.0 && \
  ln -s ~/.cargo/bin/cbindgen /usr/local/bin/cbindgen

# Trim the Rust toolchain. The bundled HTML docs (~760 MB: the Rust book, std
# docs) are never used in CI. librustc_driver-*.so and the bundled rust-lld carry
# DWARF we don't need -- these are tools we *run* (via cargo), not libraries we
# link into our artifacts, so --strip-debug does not affect anything we build.
RUN rm -rf /root/.rustup/toolchains/*/share/doc && \
    find /root/.rustup/toolchains -type f \( -name 'librustc_driver-*.so' -o -name 'rust-lld' \) \
      -exec llvm-strip --strip-debug {} +

# UV and Python packages for tests
COPY --from=uv /uv /usr/local/bin/uv
COPY pyproject.toml .
RUN uv sync
ENV PATH="/.venv/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"
