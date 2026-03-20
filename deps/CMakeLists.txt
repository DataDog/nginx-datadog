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

if(NOT TARGET inject_browser_sdk)
  add_library(inject_browser_sdk ALIAS inject_browser_sdk_ffi)
endif()
