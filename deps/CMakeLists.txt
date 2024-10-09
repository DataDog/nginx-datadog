include(corrosion.cmake)

corrosion_import_crate(MANIFEST_PATH inject-browser-sdk/lib/Cargo.toml)

corrosion_experimental_cbindgen(
  TARGET inject_browser_sdk
  HEADER_NAME injectbrowsersdk.h
  FLAGS --config ${CMAKE_SOURCE_DIR}/deps/cbindgen.toml
)

