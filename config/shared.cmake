#
# Copyright 2023 Wiimag Inc. All rights reserved.
# License: https://equals-forty-two.com/LICENSE
#
# Module that share a set of utility functions
#

cmake_minimum_required (VERSION 3.0)

# Define a function to make the first letter of a string uppercase
function(ucfirst str out)
    string(SUBSTRING ${str} 0 1 first)
    string(TOUPPER ${first} first)
    string(SUBSTRING ${str} 1 -1 rest)
    set(${out} "${first}${rest}" PARENT_SCOPE)
endfunction()

# Define a function that will generate the artifact version.git.h file.
function(generate_version_git_header root_dir)
    # Execute batch script to generate the version.h file for each supported platform
    if(WIN32)
        execute_process(COMMAND ${root_dir}/scripts/generate_git_build_info.bat ${root_dir})
    else()
        execute_process(COMMAND ${root_dir}/scripts/generate_git_build_info.sh ${root_dir})
    endif()

    # Print the artifacts/version.git.h file
    file(READ ${root_dir}/artifacts/version.git.h GIT_VERSION_FILE)
    message(STATUS "GIT_VERSION_FILE: ${GIT_VERSION_FILE}")

endfunction()
