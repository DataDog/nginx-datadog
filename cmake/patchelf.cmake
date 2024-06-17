function(patch_away_libc target)
    if (CMAKE_SYSTEM_NAME STREQUAL Darwin)
        return()
    endif()

    find_program(PATCHELF patchelf)
    if (PATCHELF STREQUAL "PATCHELF-NOTFOUND")
        message(WARNING "Patchelf not found. Can't build glibc + musl binaries")
    else()
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND patchelf --remove-needed libc.musl-${CMAKE_SYSTEM_PROCESSOR}.so.1 $<TARGET_FILE:${target}>)
    endif()
endfunction()
