cmake_minimum_required(VERSION 3.7)

# TODO: document

project(ngx_http_opentracing_module)

include(./nginx-module.cmake)
include(./nginx-opentracing.cmake)
add_subdirectory(./opentracing-cpp EXCLUDE_FROM_ALL)

add_library(ngx_http_opentracing_module SHARED)
target_sources(ngx_http_opentracing_module
    PRIVATE
        $<TARGET_OBJECTS:nginx_module>
        $<TARGET_OBJECTS:nginx_opentracing>
        $<TARGET_OBJECTS:opentracing-object>
)

add_dependencies(ngx_http_opentracing_module nginx_module nginx_opentracing opentracing-object)
