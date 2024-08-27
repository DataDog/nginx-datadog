Include(FetchContent)

if (NGINX_DATADOG_ASM_ENABLED)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.7.1
        )

    FetchContent_MakeAvailable(Catch2)

    add_executable(unit_tests json.cpp multipart.cpp urlencoded.cpp stub_nginx.c)
    add_test(NAME unit_tests COMMAND unit_tests)
    target_link_libraries(unit_tests PRIVATE ngx_http_datadog_static_lib Catch2::Catch2WithMain libddwaf_objects)
    target_compile_features(unit_tests PRIVATE cxx_std_20)
endif()
