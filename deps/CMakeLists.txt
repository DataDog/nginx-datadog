include(FetchContent)

set(INJECT_BROWSER_SDK_GIT_REF "49245e1c6ed8275990bb910125187e0aaad09e3d" CACHE STRING
    "Git tag/branch for inject-browser-sdk")

FetchContent_Declare(
  InjectBrowserSDK
  GIT_REPOSITORY https://github.com/DataDog/inject-browser-sdk.git
  GIT_TAG        ${INJECT_BROWSER_SDK_GIT_REF}
)
FetchContent_MakeAvailable(InjectBrowserSDK)

if(NOT TARGET inject_browser_sdk)
  add_library(inject_browser_sdk ALIAS inject_browser_sdk_ffi)
endif()
