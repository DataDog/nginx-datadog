Include(FetchContent)

FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.7.1
)

FetchContent_MakeAvailable(Catch2)

set(UNIT_TEST_SOURCES stub_nginx.c)

if(NGINX_DATADOG_ASM_ENABLED)
    list(APPEND UNIT_TEST_SOURCES
        json.cpp multipart.cpp urlencoded.cpp test_limiter.cpp client_ip.cpp)
endif()

if(NGINX_DATADOG_RUM_ENABLED)
    list(APPEND UNIT_TEST_SOURCES test_rum_config.cpp)
endif()

list(LENGTH UNIT_TEST_SOURCES _num_sources)
if(_num_sources GREATER 1)
    add_executable(unit_tests ${UNIT_TEST_SOURCES})
    add_test(NAME unit_tests COMMAND unit_tests)
    target_link_libraries(unit_tests PRIVATE ngx_http_datadog_static_lib Catch2::Catch2WithMain)
    target_compile_features(unit_tests PRIVATE cxx_std_20)
endif()
