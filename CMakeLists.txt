cmake_minimum_required(VERSION 3.24)

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

cmake_policy(SET CMP0068 NEW)
cmake_policy(SET CMP0135 NEW)

set(NGINX_DATADOG_VERSION 1.3.0)
project(ngx_http_datadog_module VERSION ${NGINX_DATADOG_VERSION})

option(NGINX_DATADOG_ASM_ENABLED "Build with libddwaf" ON)
option(NGINX_DATADOG_RUM_ENABLED "Build with RUM injection" OFF)
set(NGINX_SRC_DIR "" CACHE PATH "The path to a directory with nginx sources")
set(NGINX_VERSION "" CACHE STRING "The nginx version")
if (NGINX_SRC_DIR STREQUAL "" AND NGINX_VERSION STREQUAL "")
  message(FATAL_ERROR "Set NGINX_SRC_DIR or, alternatively NGINX_VERSION")
endif()
option(NGINX_PATCH_AWAY_LIBC "Patch away libc dependency" OFF)
option(NGINX_COVERAGE "Add coverage instrumentation" OFF)

if (NGINX_DATADOG_RUM_ENABLED AND NGINX_DATADOG_ASM_ENABLED)
  message(FATAL_ERROR "ASM and RUM features are mutually exclusive")
endif ()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -ggdb -O0")
  set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -ggdb -O0")
endif()

message(STATUS "nginx-datadog version=[${NGINX_DATADOG_VERSION}]")

set(NGINX_DATADOG_PRODUCTS_LIST "tracing")
if (NGINX_DATADOG_ASM_ENABLED)
  list(APPEND NGINX_DATADOG_PRODUCTS_LIST "appsec")
endif ()

if (NGINX_DATADOG_RUM_ENABLED)
  list(APPEND NGINX_DATADOG_PRODUCTS_LIST "rum-injection")
endif ()

list(JOIN NGINX_DATADOG_PRODUCTS_LIST " " NGINX_DATADOG_PRODUCTS)
unset(NGINX_DATADOG_PRODUCTS_LIST)

message(STATUS "nginx-datadog products: ${NGINX_DATADOG_PRODUCTS}")

# Make curl link against a static zlib (requires cmake 3.24)
set(ZLIB_USE_STATIC_LIBS ON)

# Prefer the build mode "release with debug info" if another mode wasn't
# specified.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE "RelWithDebInfo")
endif()

# Global compiler options
if(CMAKE_COMPILER_IS_GNUCXX)
  # This warning has a false positive. See
  # <https://gcc.gnu.org/bugzilla/show_bug.cgi?id=108088>.
  add_compile_options(-Wno-error=free-nonheap-object)

  # Looks like a false positive. Doesn't like a constructor taking
  # std::optional = nullopt and moving it to a null optional
  add_compile_options(-Wno-error=maybe-uninitialized)
endif()

# save frame pointers to make it more profiler friendly
add_compile_options(-fno-omit-frame-pointer)

# Nginx module boilerplate (code within the nginx repository)
include(./cmake/deps/nginx-module.cmake)
include(./cmake/deps/rapidjson.cmake)

add_subdirectory(./dd-trace-cpp EXCLUDE_FROM_ALL)

if(NGINX_DATADOG_ASM_ENABLED)
  # we need neither but libddwaf requires one enabled
  set(LIBDDWAF_BUILD_STATIC ON)
  set(LIBDDWAF_BUILD_SHARED OFF)
  set(LIBDDWAF_TESTING OFF)
  set(LIBDDWAF_ENABLE_LTO OFF)
  add_subdirectory(./libddwaf EXCLUDE_FROM_ALL)

  # get libddwaf version
  file(READ "./libddwaf/version" LIBDDWAF_VERSION)

  # get rules version
  file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/src/security/recommended.json ASM_RULES REGEX "rules_version")
  string(REGEX REPLACE "\"rules_version\": \"(.*)\"" "\\1" RULES_VERSION ${ASM_RULES})
  string(STRIP "${RULES_VERSION}" RULES_VERSION)
  unset(ASM_RULES)
endif()

if(NGINX_DATADOG_RUM_ENABLED)
  execute_process (
    COMMAND cargo pkgid --package inject-browser-sdk --manifest-path "${CMAKE_CURRENT_SOURCE_DIR}/deps/inject-browser-sdk/Cargo.toml"
    OUTPUT_VARIABLE RUM_SDK_INJECTOR_PKGID
  )
  string(REGEX REPLACE ".*@(.*)" "\\1" RUM_SDK_INJECTOR_VERSION ${RUM_SDK_INJECTOR_PKGID})
  string(STRIP "${RUM_SDK_INJECTOR_VERSION}" RUM_SDK_INJECTOR_VERSION)

  unset(RUM_SDK_INJECTOR_PKGID)

  add_subdirectory(deps)
endif()

include(./cmake/generate_build_id.cmake)

# Generate the build identifier.
# Known issue: this command is only run during the CMake configuration stage.
# Possible solution: <https://jonathanhamberg.com/post/cmake-embedding-git-hash/>
make_build_id(NGINX_DATADOG_BUILD_ID)
configure_file(src/version.cpp.in ${CMAKE_BINARY_DIR}/version.cpp)

# The shared library (nginx module) that we are building.
add_library(ngx_http_datadog_module SHARED)

target_sources(ngx_http_datadog_module
  PRIVATE
    src/array_util.cpp
    src/datadog_conf.cpp
    src/datadog_conf_handler.cpp
    src/datadog_context.cpp
    src/datadog_directive.cpp
    src/datadog_handler.cpp
    src/datadog_variable.cpp
    src/dd.cpp
    src/defer.cpp
    src/global_tracer.cpp
    src/ngx_event_scheduler.cpp
    src/ngx_header_reader.cpp
    src/ngx_http_datadog_module.cpp
    src/ngx_logger.cpp
    src/ngx_script.cpp
    src/request_tracing.cpp
    src/string_util.cpp
    src/tracing_library.cpp
    ${CMAKE_BINARY_DIR}/version.cpp
)

if(NGINX_DATADOG_ASM_ENABLED)
  target_sources(ngx_http_datadog_module
    PRIVATE
    src/security/blocking.cpp
    src/security/client_ip.cpp
    src/security/collection.cpp
    src/security/context.cpp
    src/security/ddwaf_obj.cpp
    src/security/header_tags.cpp
    src/security/library.cpp
    src/security/waf_remote_cfg.cpp)
  target_compile_definitions(ngx_http_datadog_module PRIVATE WITH_WAF)
endif()
if(NGINX_DATADOG_RUM_ENABLED)
  target_sources(ngx_http_datadog_module
    PRIVATE
    src/rum/config.cpp
    src/rum/injection.cpp
  )
  target_compile_definitions(ngx_http_datadog_module PRIVATE WITH_RUM)

  target_link_libraries(ngx_http_datadog_module inject_browser_sdk)

endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(ngx_http_datadog_module PRIVATE -Wall -Werror)
endif()

if(NGINX_COVERAGE)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(ngx_http_datadog_module PRIVATE
      -fprofile-instr-generate -fcoverage-mapping -mllvm -runtime-counter-relocation=true)
    target_link_options(ngx_http_datadog_module PRIVATE
      -fprofile-instr-generate -mllvm -runtime-counter-relocation=true)
    target_sources(ngx_http_datadog_module PRIVATE src/coverage_fixup.c)
  else()
    message(FATAL_ERROR "NGINX_COVERAGE is only supported with clang")
  endif()
endif()

target_include_directories(ngx_http_datadog_module
  PRIVATE
    src/
)

target_link_libraries(ngx_http_datadog_module
  dd_trace_cpp-static
  nginx_module
  rapidjson
)

if(NGINX_DATADOG_ASM_ENABLED)
  target_link_libraries(ngx_http_datadog_module libddwaf_objects)
endif()

# Remove the "lib" prefix to match NGINX convention
set_property(TARGET ngx_http_datadog_module PROPERTY PREFIX "")

if(CMAKE_COMPILER_IS_GNUCXX OR (CMAKE_CXX_COMPILER_ID MATCHES "Clang"))
  if(APPLE)
    set_property(TARGET ngx_http_datadog_module PROPERTY SUFFIX ".so")
    target_link_options(ngx_http_datadog_module PRIVATE -undefined dynamic_lookup)
  else()
    target_link_libraries(ngx_http_datadog_module -static-libstdc++ -static-libgcc)
  endif()
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(LINKER_SCRIPT ${CMAKE_SOURCE_DIR}/nginx_module.ld)
  set_target_properties(ngx_http_datadog_module PROPERTIES LINK_DEPENDS ${LINKER_SCRIPT})
  target_link_options(ngx_http_datadog_module PRIVATE "-Wl,--version-script=${LINKER_SCRIPT}")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  set(EXPORTED_SYMBOLS ${CMAKE_SOURCE_DIR}/nginx_module.ld-prime)
  set_target_properties(ngx_http_datadog_module PROPERTIES LINK_DEPENDS ${EXPORTED_SYMBOLS})
  target_link_options(ngx_http_datadog_module PRIVATE "-Wl,-exported_symbols_list" "-Wl,${EXPORTED_SYMBOLS}")
endif()

if(NGINX_PATCH_AWAY_LIBC)
  include(./cmake/patchelf.cmake)
  patch_away_libc(ngx_http_datadog_module)
endif()

if(CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo" AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
  include(./cmake/split_debug_info.cmake)
  split_debug_info(ngx_http_datadog_module)
endif()

# vim: et ts=2 sw=2:
