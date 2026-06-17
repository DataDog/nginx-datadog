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

COPY *.diff /
RUN cd llvm-project-${LLVM_VERSION}.src && \
    patch -p1 < /compiler-rt-musl-tls.diff && \
    patch -p1 < /compiler-rt-musl-msghdr.diff && \
    patch -p1 < /compiler-rt-musl-rlimit64.diff && \
    patch -p1 < /compiler-rt-musl-stat.diff && \
    patch -p1 < /libunwind-msan-unpoison-getcontext.diff && \
    patch -p1 < /libunwind-msan-dwarfinstructions-unpoison.diff
RUN cd /usr/include && \
    patch -p0 < /locale.h.diff && \
    patch -p0 < /alltypes.h.diff

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
    -DCMAKE_INSTALL_PREFIX=/usr/asan \
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

# Third build: build a SEPARATE copy of the shared libc++/libc++abi/libunwind
# (.so) libraries instrumented with MemorySanitizer + origin tracking. MSan
# requires that ALL code linked into the process -- including the C++ runtime --
# be instrumented; an uninstrumented runtime produces a flood of false
# "use-of-uninitialized-value" reports. MSan and ASan runtimes cannot coexist in
# one process, and the .so names collide with the ASan-instrumented ones already
# in /usr/lib, so this copy is installed under a distinct prefix (/usr/msan). The
# MSan toolchain branch links against /usr/msan/lib with a matching rpath.
# Note: the C++ headers are identical to the first build (same source, same
# config) so we reuse /usr/include/c++/v1; only the compiled libraries differ.
#
# We must pass -DLLVM_USE_SANITIZER=MemoryWithOrigins (not just -fsanitize=memory
# in the flags): unlike ASan there is no shared MSan runtime, so the __msan_*
# symbols the instrumentation references are resolved at runtime from the final
# executable. HandleLLVMOptions adds "-Wl,-z,defs" (--no-undefined) to every
# shared-lib link UNLESS LLVM_USE_SANITIZER is set; without it the .so links fail
# with undefined references to __msan_init / __msan_warning_with_origin_noreturn.
# Setting LLVM_USE_SANITIZER both instruments the runtimes and drops -z defs.
RUN cd llvm-project-${LLVM_VERSION}.src && mkdir -p build-msan && cd build-msan && \
    cmake -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_INSTALL_PREFIX=/usr/msan \
    -DLLVM_USE_SANITIZER=MemoryWithOrigins \
    -DCMAKE_C_COMPILER=clang \
    -DCMAKE_C_COMPILER_TARGET=${ARCH}-alpine-linux-musl \
    -DCMAKE_C_FLAGS="-fno-omit-frame-pointer -D_LIBCPP_HAS_NO_C11_ALIGNED_ALLOC=1 -fsanitize=memory -fsanitize-memory-track-origins=2" \
    -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_CXX_COMPILER_TARGET=${ARCH}-alpine-linux-musl \
    -DCMAKE_CXX_FLAGS="-fno-omit-frame-pointer -D_LIBCPP_HAS_NO_C11_ALIGNED_ALLOC=1 -fsanitize=memory -fsanitize-memory-track-origins=2 -D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE" \
    -DCMAKE_SHARED_LINKER_FLAGS="-fsanitize=memory -fsanitize-memory-track-origins=2 -rtlib=compiler-rt -unwindlib=libunwind" \
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

RUN rm -f /usr/lib/libc++experimental.a /usr/asan/lib/libc++experimental.a /usr/msan/lib/libc++experimental.a

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

# Trim the Rust toolchain. The bundled HTML docs (~760 MB: the Rust book, std
# docs) are never used in CI. librustc_driver-*.so and the bundled rust-lld carry
# DWARF we don't need -- these are tools we *run* (via cargo), not libraries we
# link into our artifacts, so --strip-debug does not affect anything we build.
# (The final stage flattens the image, so these reductions land directly.)
RUN rm -rf /root/.rustup/toolchains/*/share/doc && \
    find /root/.rustup/toolchains -type f \( -name 'librustc_driver-*.so' -o -name 'rust-lld' \) \
      -exec llvm-strip --strip-debug {} +

# Shellcheck
RUN apk add --no-cache shellcheck

# Node.js (for datadog-ci coverage upload)
RUN apk add --no-cache nodejs npm

# UV and Python packages for tests
COPY --from=uv /uv /usr/local/bin/uv
COPY pyproject.toml .
RUN uv sync
ENV PATH="/.venv/bin:$PATH"

RUN LINUX_ARCH=linux_$(echo ${ARCH} | sed 's/x86_64/amd64/;s/aarch64/arm64/') && \
  wget -qO- https://releases.hashicorp.com/vault/1.21.2/vault_1.21.2_${LINUX_ARCH}.zip | busybox unzip - && \
  chmod +x vault && mv vault /usr/local/bin/ && \
  llvm-strip --strip-debug /usr/local/bin/vault

FROM scratch
COPY --from=sysroot / /
ENV PATH="/.venv/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin"
