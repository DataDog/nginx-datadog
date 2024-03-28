add_library(nginx_module OBJECT)

set_property(TARGET nginx_module PROPERTY POSITION_INDEPENDENT_CODE ON)

function(make_nginx_conf_args input_list output_string)
endfunction()

# Build NGINX configuration arguments
separate_arguments(NGINX_CONF_ARGS NATIVE_COMMAND "${NGINX_CONF_ARGS}")

list(APPEND NGINX_CONF_ARGS
  "--add-dynamic-module=${CMAKE_SOURCE_DIR}/module/"
  "--with-compat"
)

list(REMOVE_DUPLICATES NGINX_CONF_ARGS)

## Only for printing
list(JOIN NGINX_CONF_ARGS " " NGINX_CONF_ARGS_LOG)
message(STATUS "Nginx configuration args: ${NGINX_CONF_ARGS_LOG}")

set(NGINX_MODULE_FILE ${NGINX_SRC_DIR}/objs/ngx_http_datadog_module_modules.c)
set_source_files_properties(${NGINX_MODULE_FILE} PROPERTIES GENERATED TRUE)

include(FetchContent)

set(NGINX_VERSION "" CACHE STRING "The nginx version")
if(NGINX_VERSION STREQUAL "")
    message(FATAL_ERROR "NGINX_VERSION not set")
endif()
set(NGINX_URL "https://nginx.org/download/nginx-${NGINX_VERSION}.tar.gz")

FetchContent_Declare(
    nginx
    URL ${NGINX_URL})
FetchContent_GetProperties(nginx)
if(NOT nginx_POPULATED)
    FetchContent_Populate(nginx)
endif()

set(module_file ${nginx_SOURCE_DIR}/objs/ngx_http_datadog_module_modules.c)
set_source_files_properties(${module_file} PROPERTIES GENERATED TRUE)

add_custom_command(
    OUTPUT ${module_file}
    COMMAND ./configure --add-dynamic-module="${CMAKE_SOURCE_DIR}/module/" --with-compat --with-threads
    WORKING_DIRECTORY ${nginx_SOURCE_DIR}
    COMMENT "Configuring nginx")

target_sources(nginx_module PRIVATE ${module_file})

target_include_directories(nginx_module
    SYSTEM
    PUBLIC
    ${nginx_SOURCE_DIR}/src/event
    ${nginx_SOURCE_DIR}/src/http/modules
    ${nginx_SOURCE_DIR}/src/http
    ${nginx_SOURCE_DIR}/src/os/unix
    ${nginx_SOURCE_DIR}/objs
    ${nginx_SOURCE_DIR}/src/core
    ${nginx_SOURCE_DIR}/src/event/modules)

