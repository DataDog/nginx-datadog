cmake_minimum_required(VERSION 3.7)

# TODO: document

project(ngx_http_opentracing_module)

# Nginx module boilerplate (code within the nginx repository)
include(./nginx-module.cmake)

# The nginx-opentracing nginx module sources
include(./nginx-opentracing.cmake)

# The opentracing-cpp library sources
add_subdirectory(./opentracing-cpp EXCLUDE_FROM_ALL)

# The dd-opentracing-cpp library sources
SET(BUILD_TESTING OFF "There are conflicts among the test targets.  I should probably use multiple projects.")
SET(BUILD_OBJECT ON CACHE BOOL "Build dd-opentracing-cpp objects")
SET(BUILD_SHARED OFF CACHE BOOL "Don't build dd-opentracing-cpp shared library")
set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/dd-opentracing-cpp/3rd_party/sanitizers-cmake" ${CMAKE_MODULE_PATH})
# TODO: nope SET(CMAKE_LIBRARY_PATH dd-opentracing-cpp/deps/lib CACHE STRING "override where dd-opentracing-cpp looks for built libraries")
add_subdirectory(./dd-opentracing-cpp EXCLUDE_FROM_ALL)

add_library(ngx_http_opentracing_module SHARED)
target_sources(ngx_http_opentracing_module
    PRIVATE
        $<TARGET_OBJECTS:nginx_module>
        $<TARGET_OBJECTS:nginx_opentracing>
        $<TARGET_OBJECTS:opentracing-object>
        $<TARGET_OBJECTS:dd_opentracing-object>
)

add_dependencies(ngx_http_opentracing_module 
    nginx_module 
    nginx_opentracing 
    opentracing-object
    dd_opentracing-object
)

target_link_libraries(ngx_http_opentracing_module dd_opentracing-object)
