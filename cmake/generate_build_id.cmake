# Generates a unique build identifier based on multiple criteria
# and sets the resulting identifier to the specified output variable.

# Description:
#   This function constructs a build identifier by considering the following factors:
#   1. If the build type is Debug, the identifier is prefix with "dev" and the current 
#      date in the format YYYYMMDD.
#   2. If a git command is available, the identifier includes the latest commit hash.
#      Else, the identifier includes the hostname of the machine.
#
#   The components of the identifier are joined with a hyphen (-).
#
# Parameters:
#   OUT_BUILD_ID:
#     The name of the variable where the generated build identifier will be stored.
function (make_build_id OUT_BUILD_ID)
  set(NGINX_DATADOG_BUILD_ID_LIST)

  if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    string(TIMESTAMP TODAY "%Y%m%d")

    list(APPEND NGINX_DATADOG_BUILD_ID_LIST "dev" ${TODAY})
  endif()

  find_program(GIT_CMD "git")
  if (GIT_CMD)
    execute_process(
      COMMAND git log -1 --format=%H
      WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
      OUTPUT_VARIABLE GIT_HASH
      OUTPUT_STRIP_TRAILING_WHITESPACE
    )

    list(APPEND NGINX_DATADOG_BUILD_ID_LIST ${GIT_HASH})
  else ()
    cmake_host_system_information(RESULT USER_HOSTNAME QUERY HOSTNAME)
    list(APPEND NGINX_DATADOG_BUILD_ID_LIST USER_HOSTNAME)
  endif()

  list(JOIN NGINX_DATADOG_BUILD_ID_LIST "-" BUILD_ID)
  set(${OUT_BUILD_ID} "${BUILD_ID}" PARENT_SCOPE)
endfunction ()
