include(corrosion.cmake)

corrosion_import_crate(MANIFEST_PATH inject-browser-sdk/lib/inject-browser-sdk-ffi/Cargo.toml)

corrosion_experimental_cbindgen(
  TARGET inject_browser_sdk_ffi
  HEADER_NAME injectbrowsersdk.h
  FLAGS --config ${CMAKE_SOURCE_DIR}/deps/cbindgen.toml
)

