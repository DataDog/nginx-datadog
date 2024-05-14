function(split_debug_info target_name)
    # Define the output names for the stripped binary and debug symbols
    set(debug_file "$<TARGET_FILE:${target_name}>.debug")

    # Create a post-build command to generate the debug symbols file
    add_custom_command(TARGET ${target_name} POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} --only-keep-debug $<TARGET_FILE:${target_name}> ${debug_file}
        COMMAND ${CMAKE_STRIP} --strip-debug --strip-unneeded $<TARGET_FILE:${target_name}>
        COMMAND ${CMAKE_OBJCOPY} --add-gnu-debuglink=${debug_file} $<TARGET_FILE:${target_name}>
        COMMENT "Stripping ${target_name} and creating separate debug symbols in ${debug_file}"
    )
endfunction()
