# Dockerfile for registry.ddbuild.io/ci/nginx-datadog/nginx_musl_toolchain

ARG MIRROR_REGISTRY=registry.ddbuild.io/ci/nginx-datadog/mirror/
FROM ${MIRROR_REGISTRY}ghcr.io/astral-sh/uv:0.9.28 AS uv
FROM ${MIRROR_REGISTRY}alpine:3.23.4 AS sysroot

ARG LLVM_VERSION=21.1.2
ARG ARCH

COPY CHECKSUMS /CHECKSUMS

RUN echo "Building LLVM ${LLVM_VERSION} on ${ARCH}"

RUN apk --no-cache add alpine-sdk bash binutils clang cmake compiler-rt coreutils docker-cli-buildx \
  docker-cli-compose git linux-headers lit lld llvm make musl-dev patchelf python3 samurai sudo wget xz

RUN wget https://github.com/llvm/llvm-project/releases/download/llvmorg-${LLVM_VERSION}/llvm-project-${LLVM_VERSION}.src.tar.xz && \
    grep -F "llvm-project-${LLVM_VERSION}.src.tar.xz" /CHECKSUMS | sha512sum --check - && \
    tar -xvf llvm-project-${LLVM_VERSION}.src.tar.xz

# compiler-rt-musl-tls.diff: fixes a TLS (thread-local storage) incompatibility
# between compiler-rt and musl libc on aarch64. musl uses TLS_ABOVE_TP (Variant I),
# so the thread pointer sits between the pthread descriptor and the static TLS block;
# compiler-rt's GetTls() uses the glibc-calibrated ThreadDescriptorSize() which
# computes the wrong range, causing ASAN to see TLS overlapping the stack. The patch
# uses pthread_self() directly to compute the correct gap.
COPY compiler-rt-musl-tls.diff /compiler-rt-musl-tls.diff
RUN cd llvm-project-${LLVM_VERSION}.src && patch -p1 < /compiler-rt-musl-tls.diff

# compiler-rt-musl-msghdr.diff: fixes ASAN's sendmsg/recvmsg interceptors on musl.
# ASAN's __sanitizer_msghdr uses glibc's layout where msg_iovlen and msg_controllen
# are 8-byte size_t; musl defines them as 4-byte int/socklen_t with explicit padding.
# The interceptor reads 8 bytes at msg_controllen's offset, picking up the uninitialized
# padding as the high word and computing a bogus ~81 GB length, which aborts with
# "CHECK failed: size <= 1ULL<<40". The patch adds a SANITIZER_MUSL branch with the
# correct 4-byte field widths.
COPY compiler-rt-musl-msghdr.diff /compiler-rt-musl-msghdr.diff
RUN cd llvm-project-${LLVM_VERSION}.src && patch -p1 < /compiler-rt-musl-msghdr.diff

COPY locale.h.diff /locale.h.diff
RUN cd /usr/include && patch -p0 < /locale.h.diff

COPY alltypes.h.diff /alltypes.h.diff
RUN cd /usr/include && patch -p0 < /alltypes.h.diff

RUN cd llvm-project-${LLVM_VERSION}.src && mkdir -p build && cd build && \
    cmake -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DCMAKE_C_COMPILER=clang \
    -DCMAKE_C_COMPILER_TARGET=${ARCH}-alpine-linux-musl \
    -DCMAKE_C_FLAGS="-fno-omit-frame-pointer -D_LIBCPP_HAS_NO_C11_ALIGNED_ALLOC=1" \
    -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_CXX_COMPILER_TARGET=${ARCH}-alpine-linux-musl \
    -DCMAKE_CXX_FLAGS="-fno-omit-frame-pointer -D_LIBCPP_HAS_NO_C11_ALIGNED_ALLOC=1" \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DLIBUNWIND_ENABLE_SHARED=OFF \
    -DLIBUNWIND_ENABLE_STATIC=ON \
    -DLIBUNWIND_USE_COMPILER_RT=ON \
    -DLIBCXXABI_ENABLE_SHARED=ON \
    -DLIBCXXABI_ENABLE_STATIC=ON \
    -DLIBCXXABI_USE_LLVM_UNWINDER=ON \
    -DLIBCXXABI_ENABLE_STATIC_UNWINDER=ON \
    -DLIBCXXABI_USE_COMPILER_RT=ON \
    -DLIBCXX_ENABLE_SHARED=OFF \
    -DLIBCXX_ENABLE_STATIC=ON \
    -DLIBCXX_HAS_MUSL_LIBC=ON \
    -DLIBCXX_USE_COMPILER_RT=ON \
    -DLIBCXX_ENABLE_STATIC_ABI_LIBRARY=OFF \
    -DCOMPILER_RT_CXX_LIBRARY=libcxx \
    -DCOMPILER_RT_STATIC_CXX_LIBRARY=OFF \
    -DSANITIZER_CXX_ABI=libc++ \
    -DSANITIZER_CXX_ABI_INTREE=ON \
    -DSANITIZER_USE_STATIC_CXX_ABI=OFF \
    -DCOMPILER_RT_USE_LLVM_UNWINDER=ON \
    -DSANITIZER_USE_STATIC_LLVM_UNWINDER=ON \
    -DCOMPILER_RT_USE_BUILTINS_LIBRARY=ON \
    -DCOMPILER_RT_DEFAULT_TARGET_ONLY=ON \
    -DCOMPILER_RT_INSTALL_PATH=/usr/lib/llvm${LLVM_VERSION%%.*}/lib/clang/${LLVM_VERSION%%.*} \
    -DCOMPILER_RT_BUILD_GWP_ASAN=OFF \
    -DCOMPILER_RT_BUILD_LIBFUZZER=OFF \
    -DCOMPILER_RT_BUILD_MEMPROF=OFF \
    -DCOMPILER_RT_BUILD_ORC=OFF \
    -DCOMPILER_RT_BUILD_XRAY=OFF \
    -DCOMPILER_RT_INCLUDE_TESTS=OFF \
    -DLLVM_ENABLE_RUNTIMES="compiler-rt;libcxx;libcxxabi;libunwind" \
    -DLLVM_EXTERNAL_LIT=/usr/bin/lit ../runtimes && \
    ninja -v install-unwind install-cxxabi install-cxx && \
    ninja -v install-compiler-rt

# Second build: build ONLY the shared libc++/libc++abi (.so) libraries with
# AddressSanitizer instrumentation and libc++ assertions enabled. The first
# build produces only the static archives (.a); this pass adds the shared libs.
# Having libc++.so ASAN-instrumented ensures the container-overflow annotations
# it emits are consistent with those in ASAN-instrumented consumers (e.g. the
# nginx module), eliminating false-positive container-overflow reports.
RUN cd llvm-project-${LLVM_VERSION}.src && mkdir -p build-asan && cd build-asan && \
    cmake -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DCMAKE_C_COMPILER=clang \
    -DCMAKE_C_COMPILER_TARGET=${ARCH}-alpine-linux-musl \
    -DCMAKE_C_FLAGS="-fno-omit-frame-pointer -D_LIBCPP_HAS_NO_C11_ALIGNED_ALLOC=1 -fsanitize=address" \
    -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_CXX_COMPILER_TARGET=${ARCH}-alpine-linux-musl \
    -DCMAKE_CXX_FLAGS="-fno-omit-frame-pointer -D_LIBCPP_HAS_NO_C11_ALIGNED_ALLOC=1 -fsanitize=address -D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE" \
    -DLIBCXX_ADDITIONAL_COMPILE_FLAGS="-fsanitize=vptr" \
    -DCMAKE_SHARED_LINKER_FLAGS="-fsanitize=address -fsanitize=vptr -shared-libasan -rtlib=compiler-rt -unwindlib=libunwind -Wl,-rpath=/usr/lib/llvm${LLVM_VERSION%%.*}/lib/clang/${LLVM_VERSION%%.*}/lib/linux" \
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
    -DLIBUNWIND_ENABLE_SHARED=ON \
    -DLIBUNWIND_ENABLE_STATIC=OFF \
    -DLIBUNWIND_USE_COMPILER_RT=ON \
    -DLIBCXXABI_ENABLE_SHARED=ON \
    -DLIBCXXABI_ENABLE_STATIC=OFF \
    -DLIBCXXABI_USE_LLVM_UNWINDER=ON \
    -DLIBCXXABI_ENABLE_STATIC_UNWINDER=OFF \
    -DLIBCXXABI_USE_COMPILER_RT=ON \
    -DLIBCXX_ENABLE_SHARED=ON \
    -DLIBCXX_ENABLE_STATIC=OFF \
    -DLIBCXX_HAS_MUSL_LIBC=ON \
    -DLIBCXX_USE_COMPILER_RT=ON \
    -DLIBCXX_ENABLE_STATIC_ABI_LIBRARY=OFF \
    -DLLVM_ENABLE_RUNTIMES="libcxx;libcxxabi;libunwind" \
    -DLLVM_EXTERNAL_LIT=/usr/bin/lit ../runtimes && \
    ninja -v install-unwind install-cxx install-cxxabi

# Create a stable symlink to GCC lib dir for resource_dir
RUN set -eux; \
    GCC_DIR=$(dirname $(dirname $(gcc -print-libgcc-file-name))); \
    ln -sfn "$GCC_DIR" /usr/lib/resource_dir
RUN cd /usr/lib && ln -s clang/${LLVM_VERSION%%.*}/lib/linux/libclang_rt.builtins-*.a libclang_rt.builtins.a

RUN rm -rf /llvm-project-${LLVM_VERSION}.src
RUN rm -f llvm-project-${LLVM_VERSION}.src.tar.xz

RUN mkdir -p /sysroot/${ARCH}-none-linux-musl/usr
RUN ln -s /usr/lib /sysroot/${ARCH}-none-linux-musl/usr/
RUN ln -s /usr/include /sysroot/${ARCH}-none-linux-musl/usr/
RUN ln -s /lib /sysroot/${ARCH}-none-linux-musl/
RUN ln -s /usr/lib/llvm${LLVM_VERSION%%.*}/lib/clang/${LLVM_VERSION%%.*}/lib /sysroot/${ARCH}-none-linux-musl/usr/lib/resource_dir/lib

COPY Toolchain.cmake.${ARCH} /sysroot/${ARCH}-none-linux-musl/Toolchain.cmake

# see https://github.com/llvm/llvm-project/issues/60572
RUN GCC_VER=$(gcc -dumpfullversion) && \
    GCC_DIR=/usr/lib/gcc/${ARCH}-alpine-linux-musl/${GCC_VER} && \
    mv ${GCC_DIR}/include/stdatomic.h ${GCC_DIR}/include/stdatomic.h_ && \
    cp /usr/lib/llvm${LLVM_VERSION%%.*}/lib/clang/${LLVM_VERSION%%.*}/include/stdatomic.h ${GCC_DIR}/include/stdatomic.h

COPY glibc_compat.c /sysroot/
RUN clang --sysroot /sysroot/${ARCH}-none-linux-musl/ -fpie -O2 -fno-omit-frame-pointer -ggdb3 -c /sysroot/glibc_compat.c -o /tmp/glibc_compat.o && \
    ar rcs /sysroot/${ARCH}-none-linux-musl/usr/lib/libglibc_compat.a /tmp/glibc_compat.o && \
    rm /tmp/glibc_compat.o

# Dependencies for nginx
RUN apk add --no-cache openssl-dev pcre-dev pcre2-dev perl zlib-dev

# Rust toolchain
RUN apk add --no-cache curl
RUN curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -yq --default-toolchain 1.94.0 && \
  ln -s ~/.cargo/bin/cargo /usr/bin/cargo

RUN cargo install --locked cbindgen --version 0.26.0 && \
  ln -s ~/.cargo/bin/cbindgen /usr/local/bin/cbindgen

# Shellcheck
RUN apk add --no-cache shellcheck

# Node.js (for datadog-ci coverage upload)
RUN apk add --no-cache nodejs npm

# UV and Python packages for tests
COPY --from=uv /uv /usr/local/bin/uv
COPY pyproject.toml .
RUN uv sync
ENV PATH="/.venv/bin:$PATH"

# Vault
RUN LINUX_ARCH=linux_$(echo ${ARCH} | sed 's/x86_64/amd64/;s/aarch64/arm64/') && \
  wget -qO- https://releases.hashicorp.com/vault/1.21.2/vault_1.21.2_${LINUX_ARCH}.zip | busybox unzip -
RUN chmod +x vault && mv vault /usr/local/bin/
