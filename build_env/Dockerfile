FROM alpine:3.18.3 AS sysroot

ARG LLVM_VERSION=16.0.6
ARG ARCH

COPY CHECKSUMS /CHECKSUMS

RUN echo "Building LLVM ${LLVM_VERSION} on ${ARCH}"

RUN apk --no-cache add alpine-sdk coreutils sudo bash samurai python3 linux-headers \
	compiler-rt clang llvm lld wget cmake make binutils musl-dev git patchelf xz lit
RUN wget https://github.com/llvm/llvm-project/releases/download/llvmorg-${LLVM_VERSION}/llvm-project-${LLVM_VERSION}.src.tar.xz && \
    grep -F llvm-project-${LLVM_VERSION}.src.tar.xz /CHECKSUMS | sha512sum --check && \
    tar -xvf llvm-project-${LLVM_VERSION}.src.tar.xz

COPY locale.h.diff /locale.h.diff
RUN cd /usr/include && patch -p0 < /locale.h.diff

COPY alltypes.h.diff /alltypes.h.diff
RUN cd /usr/include && patch -p0 < /alltypes.h.diff

RUN cd llvm-project-${LLVM_VERSION}.src && mkdir -p build && cd build && \
    cmake -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DCMAKE_C_COMPILER=clang \
    -DCMAKE_C_FLAGS="-fno-omit-frame-pointer -D_LIBCPP_HAS_NO_C11_ALIGNED_ALLOC=1" \
    -DCMAKE_CXX_COMPILER=clang++ \
    -DCMAKE_CXX_FLAGS="-fno-omit-frame-pointer -D_LIBCPP_HAS_NO_C11_ALIGNED_ALLOC=1" \
    -DLIBUNWIND_ENABLE_SHARED=OFF \
    -DLIBUNWIND_ENABLE_STATIC=ON \
    -DLIBUNWIND_USE_COMPILER_RT=ON \
    -DLIBCXXABI_ENABLE_SHARED=OFF \
    -DLIBCXXABI_USE_LLVM_UNWINDER=ON \
    -DLIBCXXABI_ENABLE_STATIC_UNWINDER=ON \
    -DLIBCXXABI_USE_COMPILER_RT=ON \
    -DLIBCXX_ENABLE_SHARED=OFF \
    -DLIBCXX_HAS_MUSL_LIBC=ON \
    -DLIBCXX_USE_COMPILER_RT=ON \
    -DLIBCXX_ENABLE_STATIC_ABI_LIBRARY=ON \
    -DLLVM_ENABLE_RUNTIMES="libcxx;libcxxabi;libunwind" \
    -DLLVM_EXTERNAL_LIT=/usr/bin/lit ../runtimes && \
    ninja -v install-unwind install-cxxabi install-cxx

RUN cd /usr/lib && ln -s gcc/*/*/ resource_dir
RUN cd /usr/lib && ln -s clang/${LLVM_VERSION%%.*}/lib/linux/libclang_rt.builtins-*.a libclang_rt.builtins.a

RUN rm -rf /llvm-project-${LLVM_VERSION}.src
RUN rm -f llvm-project-${LLVM_VERSION}.src.tar.xz

RUN mkdir -p /sysroot/${ARCH}-none-linux-musl/usr
RUN ln -s /usr/lib /sysroot/${ARCH}-none-linux-musl/usr/
RUN ln -s /usr/include /sysroot/${ARCH}-none-linux-musl/usr/
RUN ln -s /lib /sysroot/${ARCH}-none-linux-musl/
RUN ln -s /usr/lib/llvm16/lib/clang/16/lib /sysroot/${ARCH}-none-linux-musl/usr/lib/resource_dir/lib

COPY Toolchain.cmake.${ARCH} /sysroot/${ARCH}-none-linux-musl/Toolchain.cmake

# see https://github.com/llvm/llvm-project/issues/60572
RUN mv /usr/lib/gcc/${ARCH}-alpine-linux-musl/12.2.1/include/stdatomic.h /usr/lib/gcc/${ARCH}-alpine-linux-musl/12.2.1/include/stdatomic.h_
RUN cp /usr/lib/llvm16/lib/clang/16/include/stdatomic.h /usr/lib/gcc/${ARCH}-alpine-linux-musl/12.2.1/include/stdatomic.h

COPY glibc_compat.c /sysroot/
RUN clang --sysroot /sysroot/${ARCH}-none-linux-musl/ -fpie -O2 -fno-omit-frame-pointer \
    -ggdb3 -c /sysroot/glibc_compat.c -o /tmp/glibc_compat.o && \
    ar rcs /sysroot/${ARCH}-none-linux-musl/usr/lib/libglibc_compat.a /tmp/glibc_compat.o && \
    rm /tmp/glibc_compat.o


# Install dependencies for nginx
RUN apk add --no-cache pcre-dev pcre2-dev zlib-dev openssl-dev perl
