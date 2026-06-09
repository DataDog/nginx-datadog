cmake_minimum_required(VERSION 3.20)

project(nginx_datadog_clang_tidy LANGUAGES C CXX)

find_package(LLVM REQUIRED CONFIG)
find_package(Clang REQUIRED CONFIG)

add_library(NginxDatadogClangTidy MODULE
  NginxDatadogModule.cpp
  NginxLogFormatCheck.cpp)

target_compile_features(NginxDatadogClangTidy PRIVATE cxx_std_17)
target_include_directories(NginxDatadogClangTidy PRIVATE
  ${LLVM_INCLUDE_DIRS}
  ${CLANG_INCLUDE_DIRS})
target_compile_definitions(NginxDatadogClangTidy PRIVATE
  ${LLVM_DEFINITIONS})

if(NOT LLVM_ENABLE_RTTI)
  target_compile_options(NginxDatadogClangTidy PRIVATE -fno-rtti)
endif()

target_link_libraries(NginxDatadogClangTidy PRIVATE
  clangAST
  clangASTMatchers
  clangBasic
  clangLex
  clangTidy)
