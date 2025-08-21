include(FetchContent)

FetchContent_Declare(
  cppcodec
  GIT_REPOSITORY https://github.com/tplgy/cppcodec.git
  GIT_TAG        v0.2
)

FetchContent_GetProperties(cppcodec)
if(NOT cppcodec_POPULATED)
  FetchContent_Populate(cppcodec)
endif()

add_library(cppcodec INTERFACE)
target_include_directories(cppcodec SYSTEM INTERFACE ${cppcodec_SOURCE_DIR})
