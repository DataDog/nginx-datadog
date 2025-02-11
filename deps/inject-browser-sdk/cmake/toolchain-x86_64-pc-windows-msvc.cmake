# Unless explicitly stated otherwise all files in this repository are licensed
# under the Apache 2.0 License. This product includes software developed at
# Datadog (https://www.datadoghq.com/).
#
# Copyright 2024-Present Datadog, Inc.

# the name of the target operating system
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_VERSION 10.0)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_RC_COMPILER /usr/bin/llvm-rc)
set(CMAKE_LINKER /usr/bin/lld-link)
set(CMAKE_RANLIB /usr/bin/llvm-ranlib)
set(CMAKE_STRIP /usr/bin/strip)
set(CMAKE_AR /usr/bin/llvm-lib)
set(CMAKE_NM /usr/bin/llvm-nm)
set(CMAKE_MT /usr/bin/llvm-mt)

set(triple x86_64-pc-windows-msvc)
set(cl_flags "-Qunused-arguments -DWIN32 -D_WINDOWS -imsvc /xwin/crt/include -imsvc /xwin/sdk/include/ucrt -imsvc /xwin/sdk/include/shared -imsvc /xwin/sdk/include/um")
set(lk_flags "-libpath:/xwin/crt/lib/x86_64 -libpath:/xwin/sdk/lib/ucrt/x86_64 -libpath:/xwin/sdk/lib/um/x86_64")

set(CMAKE_C_COMPILER   /usr/bin/clang-cl)
set(CMAKE_C_COMPILER_TARGET ${triple})
set(CMAKE_C_FLAGS_INIT ${cl_flags})
set(CMAKE_C_STANDARD_LIBRARIES "")
set(CMAKE_C_COMPILER_FRONTENT_VARIANT "MSVC")

set(CMAKE_CXX_COMPILER /usr/bin/clang-cl)
set(CMAKE_CXX_COMPILER_TARGET ${triple})
set(CMAKE_CXX_FLAGS_INIT ${cl_flags})
set(CMAKE_CXX_STANDARD_LIBRARIES "")
set(CMAKE_CXX_COMPILER_FRONTENT_VARIANT "MSVC")

set(CMAKE_EXE_LINKER_FLAGS_INIT ${lk_flags})
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${lk_flags}")
set(CMAKE_STATIC_LINKER_FLAGS_INIT "${lk_flags}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${lk_flags}")

