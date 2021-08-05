cmake_minimum_required(VERSION 3.12)

# TODO: document

add_library(nginx_opentracing OBJECT)
set_property(TARGET nginx_opentracing PROPERTY POSITION_INDEPENDENT_CODE ON)

target_sources(nginx_opentracing
    PRIVATE
        nginx-opentracing/opentracing/src/opentracing_conf_handler.cpp
        nginx-opentracing/opentracing/src/load_tracer.cpp
        nginx-opentracing/opentracing/src/discover_span_context_keys.cpp
        nginx-opentracing/opentracing/src/opentracing_handler.cpp
        nginx-opentracing/opentracing/src/ngx_http_opentracing_module.cpp
        nginx-opentracing/opentracing/src/opentracing_context.cpp
        nginx-opentracing/opentracing/src/span_context_querier.cpp
        nginx-opentracing/opentracing/src/ngx_script.cpp
        nginx-opentracing/opentracing/src/extract_span_context.cpp
        nginx-opentracing/opentracing/src/request_tracing.cpp
        nginx-opentracing/opentracing/src/utility.cpp
        nginx-opentracing/opentracing/src/opentracing_directive.cpp
        nginx-opentracing/opentracing/src/opentracing_variable.cpp
)

include_directories(
    nginx-opentracing/opentracing
)
