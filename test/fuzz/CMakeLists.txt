if(ENABLE_FUZZERS)
  set(SANITIZE_FLAGS -fsanitize=fuzzer,address,undefined,leak)
  add_executable(fuzz_multipart fuzz_multipart.cpp)

  target_link_libraries(fuzz_multipart PRIVATE ngx_http_datadog_static_lib)

  target_include_directories(fuzz_multipart PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_compile_features(fuzz_multipart PRIVATE cxx_std_20)
  target_compile_options(fuzz_multipart PRIVATE ${SANITIZE_FLAGS})
  target_link_options(fuzz_multipart PRIVATE ${SANITIZE_FLAGS})

  add_test(NAME fuzz_multipart_run COMMAND fuzz_parse_multipart -runs=10000)
endif()
