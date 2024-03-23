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

add_custom_command(
  OUTPUT ${NGINX_MODULE_FILE}
  COMMAND ./configure ${NGINX_CONF_ARGS}
  WORKING_DIRECTORY ${NGINX_SRC_DIR}
  COMMENT "Configuring nginx"
)

target_sources(nginx_module
  PRIVATE
    ${NGINX_MODULE_FILE}
)

include_directories(
  PUBLIC
    ${NGINX_SRC_DIR}/src/event
    ${NGINX_SRC_DIR}/src/http/modules
    ${NGINX_SRC_DIR}/src/http
    ${NGINX_SRC_DIR}/src/os/unix
    ${NGINX_SRC_DIR}/src/core
    ${NGINX_SRC_DIR}/src/event/modules
    ${NGINX_SRC_DIR}/objs
)

