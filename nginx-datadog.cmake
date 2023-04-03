cmake_minimum_required(VERSION 3.12)

# This exposes a library of type OBJECT that contains compilation instructions
# for all code under `src/`, i.e. the source code of the Datadog nginx module.

add_library(nginx_datadog OBJECT)
set_property(TARGET nginx_datadog PROPERTY POSITION_INDEPENDENT_CODE ON)

target_sources(nginx_datadog
    PRIVATE
        src/array_util.cpp
        src/config_util.cpp
        src/datadog_conf.cpp
        src/datadog_conf_handler.cpp
        src/datadog_context.cpp
        src/datadog_directive.cpp
        src/datadog_handler.cpp
        src/datadog_variable.cpp
        src/dd.cpp
        src/defer.cpp
        src/discover_span_context_keys.cpp
        src/global_tracer.cpp
        src/log_conf.cpp
        src/ngx_event_scheduler.cpp
        src/ngx_filebuf.cpp
        src/ngx_header_reader.cpp
        src/ngx_http_datadog_module.cpp
        src/ngx_logger.cpp
        src/ngx_script.cpp
        src/propagation_header_querier.cpp
        src/request_tracing.cpp
        src/string_util.cpp
        src/tracing_library.cpp
)

target_include_directories(
    nginx_datadog
    PRIVATE
        src/
        dd-trace-cpp/src/
)
