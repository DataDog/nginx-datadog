include(corrosion.cmake)

corrosion_import_crate(MANIFEST_PATH inject-browser-sdk/lib/Cargo.toml)

corrosion_experimental_cbindgen(
  TARGET injectbrowsersdk
  HEADER_NAME injectbrowsersdk.h
  FLAGS --config ${CMAKE_SOURCE_DIR}/deps/cbindgen.toml
)
