# Unless explicitly stated otherwise all files in this repository are licensed
# under the Apache 2.0 License. This product includes software developed at
# Datadog (https://www.datadoghq.com/).
#
# Copyright 2024-Present Datadog, Inc.

FROM public.ecr.aws/b1o7r7e0/nginx_musl_toolchain:latest

ARG ARCH

# Install std crate for x86_64-windows
RUN ~/.cargo/bin/rustup target add x86_64-pc-windows-msvc

# Install Windows CRT
ENV XWIN_VERSION=0.6.5
ADD https://github.com/Jake-Shadle/xwin/releases/download/${XWIN_VERSION}/xwin-${XWIN_VERSION}-${ARCH}-unknown-linux-musl.tar.gz /tmp/xwin.tar.gz
RUN tar xzf /tmp/xwin.tar.gz -C /tmp \
 && /tmp/xwin-${XWIN_VERSION}-${ARCH}-unknown-linux-musl/xwin --accept-license splat --include-debug-libs --include-debug-symbols --output /xwin \
 && rm /tmp/xwin.tar.gz \
 && rm -rf .xwin-cache /usr/cargo/bin/xwin

ENV CC_x86_64_pc_windows_msvc="clang" \
    CXX_x86_64_pc_windows_msvc="clang" \
    AR_x86_64_pc_windows_msvc="llvm-lib" \
    CL_FLAGS="-target x86_64-pc-windows-msvc \
              -isystem /xwin/crt/include -isystem /xwin/sdk/include/ucrt -isystem /xwin/sdk/include/shared -isystem /xwin/sdk/include/um \
              -fuse-ld=lld-link" \
    RUSTFLAGS="-Lnative=/xwin/crt/lib/x86_64 -Lnative=/xwin/sdk/lib/ucrt/x86_64 -Lnative=/xwin/sdk/lib/um/x86_64" \
    CARGO_TARGET_X86_64_PC_WINDOWS_MSVC_LINKER="lld-link"

# These are separate because docker won't resolve env var defined in the same ENV block
ENV CFLAGS_x86_64_pc_windows_msvc="$CL_FLAGS" \
    CXXFLAGS_x86_64_pc_windows_msvc="$CL_FLAGS"

