# cmake/import_prebuilt_deps.cmake
#
# Imported by the main CMakeLists.txt when DEPS_BUILD_DIR is set.
# Creates targets that match what add_subdirectory() would provide:
#   - dd-trace-cpp::static
#   - libddwaf_objects  (when ASM enabled)
#   - rapidjson
#   - cppcodec

if(NOT EXISTS "${DEPS_BUILD_DIR}/deps_manifest.cmake")
  message(FATAL_ERROR
    "DEPS_BUILD_DIR (${DEPS_BUILD_DIR}) does not contain deps_manifest.cmake.\n"
    "Run the Stage 1 deps build first:\n"
    "  cmake -B ${DEPS_BUILD_DIR} cmake/deps-only && cmake --build ${DEPS_BUILD_DIR}")
endif()

include("${DEPS_BUILD_DIR}/deps_manifest.cmake")

# --- Validate WAF consistency ---
if(NGINX_DATADOG_ASM_ENABLED AND NOT DEPS_ASM_ENABLED)
  message(FATAL_ERROR
    "NGINX_DATADOG_ASM_ENABLED=ON but the deps build did not include libddwaf.\n"
    "Rebuild deps with -DNGINX_DATADOG_ASM_ENABLED=ON")
endif()

# --- Header-only deps ---
add_library(rapidjson INTERFACE)
target_include_directories(rapidjson SYSTEM INTERFACE "${DEPS_RAPIDJSON_INCLUDE_DIR}")

add_library(cppcodec INTERFACE)
target_include_directories(cppcodec SYSTEM INTERFACE "${DEPS_CPPCODEC_INCLUDE_DIR}")

# --- dd-trace-cpp::static ---
# Use the explicit library path from the manifest, plus glob for transitive
# deps (curl, zlib, etc.) that live in the dd-trace-cpp build subtree.
file(GLOB_RECURSE _dd_trace_transitive_libs "${DEPS_DD_TRACE_CPP_BINARY_DIR}/*.a")
set(_dd_trace_all_libs "${DEPS_DD_TRACE_CPP_LIB}" ${_dd_trace_transitive_libs})
list(REMOVE_DUPLICATES _dd_trace_all_libs)

if(NOT EXISTS "${DEPS_DD_TRACE_CPP_LIB}")
  message(FATAL_ERROR
    "dd-trace-cpp library not found at ${DEPS_DD_TRACE_CPP_LIB}.\n"
    "Ensure the Stage 1 build completed successfully.")
endif()

find_package(Threads REQUIRED)

add_library(dd_trace_cpp_imported INTERFACE)
target_include_directories(dd_trace_cpp_imported SYSTEM INTERFACE
  "${DEPS_DD_TRACE_CPP_SOURCE_DIR}/include"
)
target_link_libraries(dd_trace_cpp_imported INTERFACE
  ${_dd_trace_all_libs}
  Threads::Threads
)

# On Linux, dd-trace-cpp's curl needs additional system libraries.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  target_link_libraries(dd_trace_cpp_imported INTERFACE rt dl)
endif()

add_library(dd-trace-cpp::static ALIAS dd_trace_cpp_imported)

# --- libddwaf_objects ---
if(NGINX_DATADOG_ASM_ENABLED)
  # Use the explicit library path from the manifest, plus glob for any
  # transitive deps built inside the libddwaf subtree.
  file(GLOB_RECURSE _ddwaf_transitive_libs "${DEPS_LIBDDWAF_BINARY_DIR}/*.a")
  set(_ddwaf_all_libs "${DEPS_LIBDDWAF_LIB}" ${_ddwaf_transitive_libs})
  list(REMOVE_DUPLICATES _ddwaf_all_libs)

  if(NOT EXISTS "${DEPS_LIBDDWAF_LIB}")
    message(FATAL_ERROR
      "libddwaf library not found at ${DEPS_LIBDDWAF_LIB}.\n"
      "Ensure the Stage 1 build completed with ASM enabled.")
  endif()

  # In the original build, libddwaf_objects is an OBJECT library.
  # We replace it with an INTERFACE library wrapping the static archive.
  # This works because:
  #   - Linking an INTERFACE lib to an OBJECT lib propagates include dirs (same as OBJECT→OBJECT)
  #   - Linking an INTERFACE lib to a SHARED lib links the archives (same effect as OBJECT→SHARED)
  add_library(libddwaf_objects INTERFACE)
  target_include_directories(libddwaf_objects SYSTEM INTERFACE
    "${DEPS_LIBDDWAF_SOURCE_DIR}/include"
  )
  target_link_libraries(libddwaf_objects INTERFACE ${_ddwaf_all_libs})

  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(libddwaf_objects INTERFACE pthread rt dl)
  endif()
endif()

# vim: et ts=2 sw=2:
