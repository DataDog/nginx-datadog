cmake_minimum_required(VERSION 3.7)

project(ngx_http_opentracing_module)

include(./nginx-module.cmake)
include(./nginx-opentracing.cmake)

add_library(ngx_http_opentracing_module SHARED)
target_sources(ngx_http_opentracing_module
    PRIVATE
        $<TARGET_OBJECTS:nginx_module>
        $<TARGET_OBJECTS:nginx_opentracing>
)

find_library(OPENTRACING_LIB NAMES libopentracing.a)
target_link_libraries(ngx_http_opentracing_module ${OPENTRACING_LIB})

add_dependencies(ngx_http_opentracing_module ngx_module nginx_opentracing)
