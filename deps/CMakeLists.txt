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
  message(STATUS "Building inject-browser-sdk from source using corrosion")

  # Verify the source directory exists
  if(NOT EXISTS "${CMAKE_SOURCE_DIR}/deps/inject-browser-sdk/lib/inject-browser-sdk-ffi/Cargo.toml")
    message(FATAL_ERROR "inject-browser-sdk source not found at ${CMAKE_SOURCE_DIR}/deps/inject-browser-sdk\n"
      "Either provide pre-built artifacts in ${INJECT_BROWSER_SDK_PREBUILT_DIR} or clone the inject-browser-sdk repository")
  endif()

  include(corrosion.cmake)

  corrosion_import_crate(MANIFEST_PATH inject-browser-sdk/lib/inject-browser-sdk-ffi/Cargo.toml)

  corrosion_experimental_cbindgen(
    TARGET inject_browser_sdk_ffi
    HEADER_NAME injectbrowsersdk.h
    FLAGS --config ${CMAKE_SOURCE_DIR}/deps/cbindgen.toml
  )

  # Create an alias to match the pre-built target name
  add_library(inject_browser_sdk ALIAS inject_browser_sdk_ffi)
endif()
