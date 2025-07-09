cmake_minimum_required(VERSION 3.24)

set(CMAKE_C_STANDARD 17)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

cmake_policy(SET CMP0068 NEW)
cmake_policy(SET CMP0135 NEW)

set(NGINX_DATADOG_VERSION 1.6.1)
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
option(ENABLE_FUZZERS "For building fuzzers" OFF)
set(NGINX_DATADOG_FLAVOR "nginx" CACHE STRING "NGINX flavor")

if (NGINX_DATADOG_RUM_ENABLED AND NGINX_DATADOG_ASM_ENABLED)
  message(FATAL_ERROR "ASM and RUM features are mutually exclusive")
endif ()

include(CTest)

list(APPEND CMAKE_MODULE_PATH "/Users/louis.tricot/datadog-work/proxy/nginx-datadog/cmake-sbom/cmake")
include(version)
include(sbom)

# Initialize SBOM generation
sbom_generate(
  OUTPUT ${CMAKE_BINARY_DIR}/nginx-datadog-${GIT_VERSION}.spdx
  PROJECT "nginx-datadog"
  SUPPLIER "Organization: Datadog, Inc."
  SUPPLIER_URL "https://www.datadoghq.com/"
  LICENSE "Apache-2.0"
  COPYRIGHT "2022 Datadog, Inc."
  DOWNLOAD_URL "https://github.com/DataDog/nginx-datadog"
)

# Add main library target to SBOM
sbom_add(
  TARGET ngx_http_datadog_module
  LICENSE "Apache-2.0"
  SPDXID "SPDXRef-nginx-datadog-module"
)

# Add dd-trace-cpp dependency
sbom_add(
  PACKAGE "dd-trace-cpp"
  VERSION "1.0.0"
  LICENSE "Apache-2.0"
  COPYRIGHT "2022 Datadog, Inc."
  SUPPLIER "Organization: Datadog, Inc. (info@datadoghq.com)"
  DOWNLOAD_LOCATION "https://github.com/DataDog/dd-trace-cpp"
  SPDXID "SPDXRef-dd-trace-cpp"
)

# Add rapidjson dependency
sbom_add(
  PACKAGE "rapidjson"
  VERSION "1.1.0"
  LICENSE "MIT"
  SUPPLIER "Organization: Tencent (rapidjson@googlegroups.com)"
  COPYRIGHT  "2015 THL A29 Limited, a Tencent company, and Milo Yip"
  DOWNLOAD_LOCATION "https://github.com/Tencent/rapidjson"
  SPDXID "SPDXRef-rapidjson"
)

# Add nlohmann/json dependency (used by dd-trace-cpp)
sbom_add(
  PACKAGE "nlohmann/json"
  VERSION "3.11.2"
  LICENSE "MIT"
  SUPPLIER "Organization: Niels Lohmann (http://nlohmann.me)"
  COPYRIGHT "2013-2018 Niels Lohmann"
  DOWNLOAD_LOCATION "https://github.com/nlohmann/json"
  SPDXID "SPDXRef-nlohmann-json"
)

# Add libcurl dependency (used by dd-trace-cpp)
sbom_add(
  PACKAGE "curl"
  VERSION "8.8.0"
  LICENSE "curl"
  COPYRIGHT "1996 - 2025, Daniel Stenberg"
  SUPPLIER "Organization: Daniel Stenberg (daniel@haxx.se)"
  DOWNLOAD_LOCATION "https://github.com/curl/curl"
  SPDXID "SPDXRef-curl"
)

# Add nginx dependency
sbom_add(
  PACKAGE "nginx"
  VERSION "${NGINX_VERSION}"
  LICENSE "BSD-2-Clause"
  COPYRIGHT "2011-2025 Nginx, Inc."
  SUPPLIER "Organization: Nginx, Inc."
  DOWNLOAD_LOCATION "https://nginx.org/"
  SPDXID "SPDXRef-nginx"
)

# Add libddwaf dependency if ASM is enabled
if(NGINX_DATADOG_ASM_ENABLED)
  sbom_add(
    PACKAGE "libddwaf"
    VERSION "${LIBDDWAF_VERSION}"
    LICENSE "Apache-2.0"
    SUPPLIER "Organization: Datadog, Inc."
    DOWNLOAD_LOCATION "https://github.com/DataDog/libddwaf"
    SPDXID "SPDXRef-libddwaf"
  )
endif()

# Add inject-browser-sdk dependency if RUM is enabled
if(NGINX_DATADOG_RUM_ENABLED)
  sbom_add(
    PACKAGE "inject-browser-sdk"
    VERSION "${RUM_SDK_INJECTOR_VERSION}"
    LICENSE "Apache-2.0"
    SUPPLIER "Organization: Datadog, Inc."
    DOWNLOAD_LOCATION "https://github.com/DataDog/browser-sdk"
    SPDXID "SPDXRef-inject-browser-sdk"
  )
endif()

# Add system dependencies
sbom_add(
  PACKAGE "libc"
  VERSION "system"
  LICENSE "NOASSERTION"
  SUPPLIER "Organization: System Vendor"
  SPDXID "SPDXRef-libc"
)

sbom_add(
  PACKAGE "libstdc++"
  VERSION "system"
  LICENSE "GPL-3.0"
  SUPPLIER "Organization: Free Software Foundation"
  SPDXID "SPDXRef-libstdc++"
)

# Add libc++ dependency (used with clang)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  sbom_add(
    PACKAGE "libc++"
    VERSION "system"
    LICENSE "Apache-2.0"
    SUPPLIER "Organization: LLVM Project (https://llvm.org/)"
    SPDXID "SPDXRef-libcxx"
  )
endif()

# Finalize SBOM generation
sbom_finalize(VERIFY)

# Install the SBOM file
install(FILES ${CMAKE_BINARY_DIR}/nginx-datadog-${GIT_VERSION}.spdx
  DESTINATION .
  OPTIONAL
)

# Add a custom target for SBOM generation without install
add_custom_target(sbom
  COMMAND ${CMAKE_COMMAND} -E echo "Generating SBOM file..."
  COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target ngx_http_datadog_static_lib
  COMMAND ${CMAKE_COMMAND} --install ${CMAKE_BINARY_DIR} --prefix ${CMAKE_BINARY_DIR}
  COMMAND ${CMAKE_COMMAND} -E echo "SBOM generated: ${CMAKE_BINARY_DIR}/nginx-datadog-${GIT_VERSION}.spdx"
  COMMENT "Generating SBOM file: ${CMAKE_BINARY_DIR}/nginx-datadog-${GIT_VERSION}.spdx"
  DEPENDS ngx_http_datadog_module ngx_http_datadog_static_lib
)

if(BUILD_TESTING)
  add_subdirectory(test/unit)
endif()
if(ENABLE_FUZZERS)
  add_subdirectory(test/fuzz)
endif()

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

# Define `DD_NGINX_FLAVOR`

target_sources(ngx_http_datadog_module PRIVATE ${CMAKE_BINARY_DIR}/version.cpp)

add_library(ngx_http_datadog_objs OBJECT)
set_target_properties(ngx_http_datadog_objs PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_definitions(ngx_http_datadog_objs PUBLIC DD_NGINX_FLAVOR="${NGINX_DATADOG_FLAVOR}")
target_sources(ngx_http_datadog_objs
  PRIVATE
    src/common/variable.cpp
    src/common/directives.cpp
    src/common/headers.cpp
    src/array_util.cpp
    src/datadog_conf.cpp
    src/datadog_conf_handler.cpp
    src/datadog_context.cpp
    src/datadog_directive.cpp
    src/datadog_handler.cpp
    src/datadog_variable.cpp
    src/tracing/directives.cpp
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
)

if(NGINX_DATADOG_ASM_ENABLED)
  target_sources(ngx_http_datadog_objs
    PRIVATE
    src/security/blocking.cpp
    src/security/body_parse/body_json.cpp
    src/security/body_parse/body_multipart.cpp
    src/security/body_parse/body_parsing.cpp
    src/security/body_parse/header.cpp
    src/security/client_ip.cpp
    src/security/collection.cpp
    src/security/directives.cpp
    src/security/context.cpp
    src/security/ddwaf_obj.cpp
    src/security/decode.cpp
    src/security/header_tags.cpp
    src/security/library.cpp
    src/security/waf_remote_cfg.cpp)
  target_compile_definitions(ngx_http_datadog_objs PUBLIC WITH_WAF)
endif()

if(NGINX_DATADOG_RUM_ENABLED)
  target_sources(ngx_http_datadog_objs
    PRIVATE
    src/rum/config.cpp
    src/rum/injection.cpp
    src/rum/telemetry.cpp
  )
  target_compile_definitions(ngx_http_datadog_objs PUBLIC WITH_RUM)
endif()

if(ENABLE_FUZZERS)
  target_compile_options(ngx_http_datadog_objs PUBLIC -fsanitize=fuzzer,address,undefined,leak)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(ngx_http_datadog_objs PRIVATE -Wall -Werror)
endif()

if(NGINX_COVERAGE)
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(ngx_http_datadog_objs PUBLIC
      -fprofile-instr-generate -fcoverage-mapping -mllvm -runtime-counter-relocation=true)
    target_link_options(ngx_http_datadog_objs INTERFACE
      -fprofile-instr-generate -mllvm -runtime-counter-relocation=true)
    target_sources(ngx_http_datadog_module PRIVATE src/coverage_fixup.c)
    if(APPLE)
      # pass --coverage so the linker is instructed to link against libclang_rt.profile_osx.
      target_link_options(ngx_http_datadog_objs INTERFACE -v --coverage)
    endif()
  else()
    message(FATAL_ERROR "NGINX_COVERAGE is only supported with clang")
  endif()
endif()

target_include_directories(ngx_http_datadog_objs
  PUBLIC
    src/
)

target_link_libraries(ngx_http_datadog_objs PUBLIC
  dd_trace_cpp-static
  nginx_module
  rapidjson
)
target_link_libraries(ngx_http_datadog_module PRIVATE
  ngx_http_datadog_objs
  nginx_module # objects don't come transitively from ngx_http_datadog_objs
)

if(NGINX_DATADOG_ASM_ENABLED)
  target_link_libraries(ngx_http_datadog_objs PUBLIC libddwaf_objects)
  target_link_libraries(ngx_http_datadog_module PUBLIC libddwaf_objects)
endif()

if(NGINX_DATADOG_RUM_ENABLED)
  target_link_libraries(ngx_http_datadog_objs PUBLIC inject_browser_sdk)
  target_link_libraries(ngx_http_datadog_module PUBLIC inject_browser_sdk)
endif()

# Remove the "lib" prefix to match NGINX convention
set_property(TARGET ngx_http_datadog_module PROPERTY PREFIX "")

if(CMAKE_COMPILER_IS_GNUCXX OR (CMAKE_CXX_COMPILER_ID MATCHES "Clang"))
  if(APPLE)
    set_property(TARGET ngx_http_datadog_module PROPERTY SUFFIX ".so")
    target_link_options(ngx_http_datadog_module PRIVATE -undefined dynamic_lookup)
  else()
    target_link_options(ngx_http_datadog_module PRIVATE -static-libstdc++ -static-libgcc)
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

add_library(ngx_http_datadog_static_lib STATIC EXCLUDE_FROM_ALL)
target_link_libraries(ngx_http_datadog_static_lib PUBLIC ngx_http_datadog_objs)

# vim: et ts=2 sw=2:
