# Unless explicitly stated otherwise all files in this repository are licensed
# under the Apache 2.0 License. This product includes software developed at
# Datadog (https://www.datadoghq.com/).
#
# Copyright 2024-Present Datadog, Inc.

cmake_minimum_required(VERSION 3.24)

set(IIS_INJECTOR_VERSION_MAJ $ENV{MAJ_VER})
if (NOT IIS_INJECTOR_VERSION_MAJ)
  set(IIS_INJECTOR_VERSION_MAJ 0)
endif ()

set(IIS_INJECTOR_VERSION_MIN $ENV{MAJ_MIN})
if (NOT IIS_INJECTOR_VERSION_MIN)
  set(IIS_INJECTOR_VERSION_MIN 1)
endif ()

set(IIS_INJECTOR_VERSION_PATCH $ENV{MAJ_PATCH})
if (NOT IIS_INJECTOR_VERSION_PATCH)
  set(IIS_INJECTOR_VERSION_PATCH 0)
endif ()

set(IIS_INJECTOR_VERSION ${IIS_INJECTOR_VERSION_MAJ}.${IIS_INJECTOR_VERSION_MIN}.${IIS_INJECTOR_VERSION_PATCH})

project(iis-injector LANGUAGES CXX VERSION ${IIS_INJECTOR_VERSION})

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(BUILD_STATIC_LIBS ON)
set(BUILD_SHARED_LIBS OFF)

option(IIS_BUILD_TESTS "Build unit tests" OFF)
option(IIS_INJECTOR_STATIC_CRT "Build dd-trace-cpp with static CRT with MSVC" ON)
option(IIS_INJECTOR_DEBUG_BREAK "Set breakpoint at startup" OFF)

macro(get_WIN32_WINNT version)
  if(CMAKE_SYSTEM_VERSION)
    set(ver ${CMAKE_SYSTEM_VERSION})
    string(REGEX MATCH "^([0-9]+).([0-9])" ver ${ver})
    string(REGEX MATCH "^([0-9]+)" verMajor ${ver})

    # Check for Windows 10, b/c we'll need to convert to hex 'A'.
    if("${verMajor}" MATCHES "10")
      set(verMajor "A")
      string(REGEX REPLACE "^([0-9]+)" ${verMajor} ver ${ver})
    endif()

    string(REPLACE "." "" ver ${ver})
    # Prepend each digit with a zero.
    string(REGEX REPLACE "([0-9A-Z])" "0\\1" ver ${ver})
    set(${version} "0x${ver}")
  endif()
endmacro()

get_WIN32_WINNT(win_ver)

if (IIS_INJECTOR_STATIC_CRT)
  set(DD_TRACE_STATIC_CRT ON)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} /MT")
  set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} /MTd")
endif ()

# corrosion
include(FetchContent)

FetchContent_Declare(
    Corrosion
    GIT_REPOSITORY https://github.com/corrosion-rs/corrosion.git
    GIT_TAG v0.5 # Optionally specify a commit hash, version tag or branch here
)
# Set any global configuration variables such as `Rust_TOOLCHAIN` before this line!
FetchContent_MakeAvailable(Corrosion)

# rapidjson
FetchContent_Declare(
  rapidjson
  GIT_REPOSITORY https://github.com/Tencent/rapidjson.git
  GIT_TAG        ab1842a2dae061284c0a62dca1cc6d5e7e37e346
)

FetchContent_GetProperties(rapidjson)
if(NOT rapidjson_POPULATED)
  FetchContent_Populate(rapidjson)
endif()

add_library(rapidjson INTERFACE)
target_include_directories(rapidjson SYSTEM INTERFACE ${rapidjson_SOURCE_DIR}/include)

## dd-trace-cpp
set(DD_TRACE_STATIC_CRT ON)
FetchContent_Declare(
  dd-trace-cpp
  GIT_REPOSITORY https://github.com/DataDog/dd-trace-cpp
  GIT_TAG        febab817537a86d4c5cdfcc2919b3c2561923266 
  GIT_SHALLOW    OFF
  GIT_PROGRESS   ON
)

FetchContent_MakeAvailable(dd-trace-cpp)

corrosion_import_crate(
  MANIFEST_PATH ${CMAKE_SOURCE_DIR}/../Cargo.toml
  CRATE_TYPES staticlib
)

corrosion_experimental_cbindgen(
  TARGET inject_browser_sdk_ffi
  HEADER_NAME injectbrowsersdk.h
  FLAGS --config ${CMAKE_SOURCE_DIR}/cbindgen.toml
)

add_library(iis_injector-objects OBJECT)
add_library(iis_injector::obj ALIAS iis_injector-objects)

target_sources(iis_injector-objects
  PRIVATE
    src/entrypoint.cpp
    src/global_module.cpp
    src/http_module.cpp
    src/module_context.cpp
    src/telemetry.cpp
    src/ruminjector.rc
)

find_program(MC mc)

if (MC STREQUAL "MC-NOTFOUND")
  message(WARNING "mc not found. Can't compile the message text")
else ()
  add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/generated/event_provider.rc
    COMMAND mc -h ${CMAKE_CURRENT_SOURCE_DIR}/src -r ${CMAKE_BINARY_DIR}/generated ${CMAKE_CURRENT_SOURCE_DIR}/src/event_provider.mc
    COMMENT "Compiling message text"
  )

  target_sources(iis_injector-objects
    PRIVATE
      ${CMAKE_BINARY_DIR}/generated/event_provider.rc
  )
endif()

target_include_directories(iis_injector-objects
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_options(iis_injector-objects
  PRIVATE
    /W4
    /DUNICODE
    /D_CRT_SECURE_NO_WARNINGS
    /D_WIN32_WINNT=${win_ver}
    /DMAJ_VER=${IIS_INJECTOR_VERSION_MAJ}
    /DMIN_VER=${IIS_INJECTOR_VERSION_MIN}
    /DPATCH_VER=${IIS_INJECTOR_VERSION_PATCH}
)

if (IIS_INJECTOR_DEBUG_BREAK)
  target_compile_options(iis_injector-objects
    PRIVATE
      /DDD_INJECTOR_BREAK=1
  )
endif ()

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
  target_compile_options(iis_injector-objects
    PRIVATE
      # Embedded debug information in binaries (no pdb)
      /Z7
  )
endif ()

target_link_libraries(iis_injector-objects 
  PUBLIC
  inject_browser_sdk_ffi dd_trace::static rapidjson)

target_link_options(iis_injector-objects
  PRIVATE
    ws2_32.lib
)

## shared lib
add_library(iis_injector SHARED)
target_link_libraries(iis_injector iis_injector-objects)

## Tests
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
)

set(gtest_force_shared_crt OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

if (IIS_BUILD_TESTS)
  enable_testing()
  add_subdirectory(test/unittests)
endif ()
