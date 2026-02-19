# Check if we have pre-built artifacts from docker bake
set(INJECT_BROWSER_SDK_PREBUILT_DIR "${CMAKE_SOURCE_DIR}/deps/inject-browser-sdk-prebuilt" CACHE PATH
    "Path to pre-built inject-browser-sdk artifacts (libinjectbrowsersdk.a and injectbrowsersdk.h)")

if(EXISTS "${INJECT_BROWSER_SDK_PREBUILT_DIR}/libinjectbrowsersdk.a" AND
   EXISTS "${INJECT_BROWSER_SDK_PREBUILT_DIR}/injectbrowsersdk.h")
  message(STATUS "Using pre-built inject-browser-sdk artifacts from ${INJECT_BROWSER_SDK_PREBUILT_DIR}")

  # Create an imported static library
  add_library(inject_browser_sdk STATIC IMPORTED GLOBAL)
  set_target_properties(inject_browser_sdk PROPERTIES
    IMPORTED_LOCATION "${INJECT_BROWSER_SDK_PREBUILT_DIR}/libinjectbrowsersdk.a"
  )

  # Create an interface target for the header
  add_library(inject_browser_sdk_headers INTERFACE)
  target_include_directories(inject_browser_sdk_headers INTERFACE "${INJECT_BROWSER_SDK_PREBUILT_DIR}")

  # Link the header target to the main target
  target_link_libraries(inject_browser_sdk INTERFACE inject_browser_sdk_headers)

else()
  message(STATUS "Fetching inject-browser-sdk via FetchContent")

  include(FetchContent)

  set(INJECT_BROWSER_SDK_GIT_REF "49245e1c6ed8275990bb910125187e0aaad09e3d" CACHE STRING
      "Git tag/branch for inject-browser-sdk")

  FetchContent_Declare(
    InjectBrowserSDK
    GIT_REPOSITORY https://github.com/DataDog/inject-browser-sdk.git
    GIT_TAG        ${INJECT_BROWSER_SDK_GIT_REF}
  )
  FetchContent_MakeAvailable(InjectBrowserSDK)

  # Create an alias to match the pre-built target name
  if(NOT TARGET inject_browser_sdk)
    add_library(inject_browser_sdk ALIAS inject_browser_sdk_ffi)
  endif()
endif()
