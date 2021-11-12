cmake_minimum_required(VERSION 3.12)

# TODO: document

add_library(nginx_datadog OBJECT)
set_property(TARGET nginx_datadog PROPERTY POSITION_INDEPENDENT_CODE ON)

target_sources(nginx_datadog
    PRIVATE
        src/datadog_conf_handler.cpp
        src/load_tracer.cpp
        src/discover_span_context_keys.cpp
        src/datadog_handler.cpp
        src/ngx_http_datadog_module.cpp
        src/datadog_context.cpp
        src/span_context_querier.cpp
        src/ngx_filebuf.cpp
        src/ngx_script.cpp
        src/extract_span_context.cpp
        src/request_tracing.cpp
        src/config_util.cpp
        src/utility.cpp
        src/datadog_directive.cpp
        src/datadog_variable.cpp
)

include_directories(
    src/
)
