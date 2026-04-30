set(INJECT_BROWSER_SDK_NO_DEFAULT_FEATURES OFF CACHE BOOL
    "Build inject-browser-sdk without default features")

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/inject-browser-sdk/CMakeLists.txt")
  message(FATAL_ERROR
    "inject-browser-sdk submodule is missing. Run:\n"
    "  git submodule update --init --recursive deps/inject-browser-sdk")
endif()

add_subdirectory(inject-browser-sdk)

# Pass cross-compilation sysroot/target flags to Rust cc crate builds
# (e.g. aws-lc-sys) that invoke the C compiler via CFLAGS.
if(CMAKE_SYSROOT AND CMAKE_C_COMPILER_TARGET AND COMMAND corrosion_set_env_vars)
  set(_cross_flags "--sysroot=${CMAKE_SYSROOT} --target=${CMAKE_C_COMPILER_TARGET} -fuse-ld=lld -rtlib=compiler-rt")
  corrosion_set_env_vars(inject_browser_sdk_ffi
    "CFLAGS_x86_64_unknown_linux_musl=${_cross_flags}"
    "CFLAGS_aarch64_unknown_linux_musl=${_cross_flags}"
  )
endif()

if(NOT TARGET inject_browser_sdk)
  add_library(inject_browser_sdk ALIAS inject_browser_sdk_ffi)
endif()
