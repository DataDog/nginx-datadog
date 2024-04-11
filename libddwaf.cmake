set(LIBDDWAF_VERSION 1.16.0)
if(DEFINED LIBDDWAF_CUSTOM_DIR)
    set(libddwaf_DIR ${LIBDDWAF_CUSTOM_DIR}/share/cmake/libddwaf)
else()
    if (CMAKE_SYSTEM_PROCESSOR STREQUAL "")
        # CMAKE_SYSTEM_PROCESSOR is populated with uname -p
        # Something this prints "unknown"
        execute_process(
            COMMAND uname -m
            OUTPUT_VARIABLE CMAKE_SYSTEM_PROCESSOR
            OUTPUT_STRIP_TRAILING_WHITESPACE)
    endif()
    if(CMAKE_SYSTEM_NAME STREQUAL Linux)
        if(CMAKE_SYSTEM_PROCESSOR STREQUAL x86_64)
            set(LIBDDWAF_VARIANT x86_64-linux-musl)
        elseif (CMAKE_SYSTEM_PROCESSOR STREQUAL aarch64)
            set(LIBDDWAF_VARIANT aarch64-linux-musl)
        else()
            message(FATAL_ERROR "Unsupported architecture: ${CMAKE_SYSTEM_PROCESSOR}")
        endif()
    elseif (CMAKE_SYSTEM_NAME STREQUAL Darwin)
        if (CMAKE_SYSTEM_PROCESSOR STREQUAL arm64 OR CMAKE_OSX_ARCHITECTURES STREQUAL arm64)
            set(LIBDDWAF_VARIANT darwin-arm64)
        elseif (CMAKE_SYSTEM_PROCESSOR STREQUAL x86_64)
            set(LIBDDWAF_VARIANT darwin-x86_64)
        else()
            message(FATAL_ERROR "Unsupported architecture: ${CMAKE_SYSTEM_PROCESSOR}")
        endif()
    else()
        message(FATAL_ERROR "Unsupported platform: ${CMAKE_SYSTEM_NAME}")
    endif()
    if (CMAKE_VERSION VERSION_GREATER_EQUAL "3.24.0")
        cmake_policy(SET CMP0135 NEW)
    endif()
    FetchContent_Declare(
            libddwaf
            URL https://github.com/DataDog/libddwaf/releases/download/${LIBDDWAF_VERSION}/libddwaf-${LIBDDWAF_VERSION}-${LIBDDWAF_VARIANT}.tar.gz
    )
    FetchContent_MakeAvailable(libddwaf)
    set(libddwaf_DIR ${libddwaf_SOURCE_DIR}/share/cmake/libddwaf)
endif()
