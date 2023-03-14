#
# Copyright 2023 Wiimag Inc. All rights reserved.
# License: https://equals-forty-two.com/LICENSE
#
# Module that share a set of utility functions
#

cmake_minimum_required (VERSION 3.0)

#
# Define a function to make the first letter of a string uppercase
# 
# Usage: 
#   ucfirst("hello" out)
#   message(STATUS "out: ${out}")
#   # out: Hello
#
function(ucfirst str out)
    string(SUBSTRING ${str} 0 1 first)
    string(TOUPPER ${first} first)
    string(SUBSTRING ${str} 1 -1 rest)
    set(${out} "${first}${rest}" PARENT_SCOPE)
endfunction()

#
# Define a function that will generate the artifact version.git.h file.
# This file contains the git commit hash and the git branch name.
#
# Usage:
#   generate_version_git_header(${CMAKE_CURRENT_SOURCE_DIR})
#
function(generate_version_git_header root_dir)
    
    # Create the root artifacts directory if it doesn't exist
    if(NOT EXISTS ${root_dir}/artifacts)
        file(MAKE_DIRECTORY ${root_dir}/artifacts)
    endif()

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

#
# Set framework output directories
#
function(target_set_framework_output_dirs AppId ROOT_DIR)

    set_target_properties(${AppId} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${ROOT_DIR}/build/.)
    set_target_properties(${AppId} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_DEBUG ${ROOT_DIR}/build/.)
    set_target_properties(${AppId} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELEASE ${ROOT_DIR}/build/.)
    set_target_properties(${AppId} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_DEPLOY ${ROOT_DIR}/build/.)

    if (MSVC)

        # Make sure we have the pdb in the same folder.
        set_target_properties(${AppId} PROPERTIES PDB_OUTPUT_DIRECTORY ${ROOT_DIR}/build/.)
        set_target_properties(${AppId} PROPERTIES PDB_OUTPUT_DIRECTORY_DEBUG ${ROOT_DIR}/build/.)
        set_target_properties(${AppId} PROPERTIES PDB_OUTPUT_DIRECTORY_RELEASE ${ROOT_DIR}/build/.)
        set_target_properties(${AppId} PROPERTIES PDB_OUTPUT_DIRECTORY_DEPLOY ${ROOT_DIR}/build/.)

        # Make sure we have the map in the same folder.
        set_target_properties(${AppId} PROPERTIES MAP_OUTPUT_DIRECTORY ${ROOT_DIR}/build/.)
        set_target_properties(${AppId} PROPERTIES MAP_OUTPUT_DIRECTORY_DEBUG ${ROOT_DIR}/build/.)
        set_target_properties(${AppId} PROPERTIES MAP_OUTPUT_DIRECTORY_RELEASE ${ROOT_DIR}/build/.)
        set_target_properties(${AppId} PROPERTIES MAP_OUTPUT_DIRECTORY_DEPLOY ${ROOT_DIR}/build/.)

    elseif(APPLE)

    endif()
endfunction()

function(set_executable_framework_linker_flags CMAKE_EXE_LINKER_FLAGS CMAKE_EXE_LINKER_FLAGS_DEBUG CMAKE_EXE_LINKER_FLAGS_RELEASE CMAKE_EXE_LINKER_FLAGS_DEPLOY)

    # Capture parent scope linker flags
    set(${CMAKE_EXE_LINKER_FLAGS} "${CMAKE_EXE_LINKER_FLAGS}")

    if (MSVC)

        # Ignore specific default libraries msvcrt.lib;libcmt.lib in debug for MSVC
        set(${CMAKE_EXE_LINKER_FLAGS_DEBUG} "${CMAKE_EXE_LINKER_FLAGS_DEBUG} /NODEFAULTLIB:msvcrt.lib /NODEFAULTLIB:libcmt.lib")

    elseif(XCODE)

        # Link with core libraries
        set(${CMAKE_EXE_LINKER_FLAGS} "${CMAKE_EXE_LINKER_FLAGS} -framework CoreFoundation -framework CoreServices")

        # Link with carbon library
        set(${CMAKE_EXE_LINKER_FLAGS} "${CMAKE_EXE_LINKER_FLAGS} -framework Carbon")

        # Link with Cocoa library
        set(${CMAKE_EXE_LINKER_FLAGS} "${CMAKE_EXE_LINKER_FLAGS} -framework Cocoa")

        # Link with IOKit library
        set(${CMAKE_EXE_LINKER_FLAGS} "${CMAKE_EXE_LINKER_FLAGS} -framework IOKit")

        # Link with Metal library
        set(${CMAKE_EXE_LINKER_FLAGS} "${CMAKE_EXE_LINKER_FLAGS} -weak_framework Metal -weak_framework MetalKit -framework QuartzCore")

    endif()

endfunction()
