set(whitelist
    libc.so.6
    libdl.so.2
    libm.so.6
    libpthread.so.0)

execute_process(
    COMMAND "${READELF}" --dynamic "${LIBRARY}"
    OUTPUT_VARIABLE dynamic_section
    COMMAND_ERROR_IS_FATAL ANY)

string(REGEX MATCHALL
    "Shared library: \\[[^]]+\\]"
    needed_entries
    "${dynamic_section}")
foreach(entry IN LISTS needed_entries)
    string(REGEX REPLACE "Shared library: \\[([^]]+)\\]" "\\1" library "${entry}")
    if(NOT library IN_LIST whitelist)
        message(FATAL_ERROR
            "${LIBRARY} has a non-whitelisted dependency: ${library}")
    endif()
endforeach()
