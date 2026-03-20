include(FetchContent)

set(INJECT_BROWSER_SDK_GIT_REF "587b071" CACHE STRING
    "Git tag/branch for inject-browser-sdk")

set(INJECT_BROWSER_SDK_NO_DEFAULT_FEATURES OFF CACHE BOOL
    "Build inject-browser-sdk without default features")

FetchContent_Declare(
  InjectBrowserSDK
  GIT_REPOSITORY https://github.com/DataDog/inject-browser-sdk.git
  GIT_TAG        ${INJECT_BROWSER_SDK_GIT_REF}
)
FetchContent_MakeAvailable(InjectBrowserSDK)

# Pass cross-compilation sysroot/target flags to Rust cc crate builds
# (e.g. aws-lc-sys) that invoke the C compiler via CFLAGS.
if(CMAKE_SYSROOT AND CMAKE_C_COMPILER_TARGET AND COMMAND corrosion_set_env_vars)
  string(REPLACE "-" "_" _rust_env_target "${Rust_CARGO_TARGET}")
  corrosion_set_env_vars(inject_browser_sdk_ffi
    "CFLAGS_${_rust_env_target}=--sysroot=${CMAKE_SYSROOT} --target=${CMAKE_C_COMPILER_TARGET}"
  )
endif()

if(NOT TARGET inject_browser_sdk)
  add_library(inject_browser_sdk ALIAS inject_browser_sdk_ffi)
endif()
