include(FetchContent)

FetchContent_Declare(
  rapidjson
  GIT_REPOSITORY https://github.com/Tencent/rapidjson.git
  GIT_TAG        ab1842a2dae061284c0a62dca1cc6d5e7e37e346
)

FetchContent_GetProperties(rapidjson)
if(NOT rapidjson_POPULATED)
  FetchContent_Populate(rapidjson)
endif()

add_library(rapidjson INTERFACE)
target_include_directories(rapidjson SYSTEM INTERFACE ${rapidjson_SOURCE_DIR}/include)
