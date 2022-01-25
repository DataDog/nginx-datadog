cmake_minimum_required(VERSION 3.12)

# TODO: document

add_library(nginx_datadog OBJECT)
set_property(TARGET nginx_datadog PROPERTY POSITION_INDEPENDENT_CODE ON)

target_sources(nginx_datadog
    PRIVATE
        src/config_util.cpp
        src/datadog_conf.cpp
        src/datadog_conf_handler.cpp
        src/datadog_context.cpp
        src/datadog_directive.cpp
        src/datadog_handler.cpp
        src/datadog_variable.cpp
        src/defer.cpp
        src/discover_span_context_keys.cpp
        src/extract_span_context.cpp
        src/json.cpp
        src/load_tracer.cpp
        src/log_conf.cpp
        src/ngx_filebuf.cpp
        src/ngx_http_datadog_module.cpp
        src/ngx_script.cpp
        src/ot.cpp
        src/request_tracing.cpp
        src/span_context_querier.cpp
        src/string_view.cpp
        src/tracing_library.cpp
        src/utility.cpp
)

include_directories(
    src/
)
