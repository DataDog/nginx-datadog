include(FetchContent)

set(INJECT_BROWSER_SDK_GIT_REF "758c1aba91950461f6a617c626930a7ba8da3014" CACHE STRING
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

# Forward version variables from the SDK to the parent (main CMakeLists.txt).
set(INJECT_BROWSER_SDK_VERSION "${INJECT_BROWSER_SDK_VERSION}" PARENT_SCOPE)
set(INJECT_BROWSER_SDK_GIT_SHA "${INJECT_BROWSER_SDK_GIT_SHA}" PARENT_SCOPE)
set(INJECT_BROWSER_SDK_GIT_SHA_SHORT "${INJECT_BROWSER_SDK_GIT_SHA_SHORT}" PARENT_SCOPE)
set(INJECT_BROWSER_SDK_COMBINED_VERSION "${INJECT_BROWSER_SDK_COMBINED_VERSION}" PARENT_SCOPE)
