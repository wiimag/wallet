#
# Copyright 2023 Wiimag Inc. All rights reserved.
# License: https://equals-forty-two.com/LICENSE
#
# CMake entry point to build the project solutions
#
# Here's an example of how to use this file using the latest version of Visual Studio:
#   mkdir projects/.build
#   cd projects/.build
#   cmake -G "Visual Studio 17 2022" -A x64 ../..
#
# Here's an example of how to use this file using the latest version of Xcode:
#   mkdir projects/.build
#   cd projects/.build
#   cmake -G "Xcode" ../..
#
# Here's an example how to generate and compile the Visual Studio solution
#   mkdir projects/.build
#   cd projects/.build
#   cmake -G "Visual Studio 17 2022" -A x64 ../..
#   cmake --build . --config Release
#

cmake_minimum_required (VERSION 3.0)

# Get the project name from the current directory name
get_filename_component(ProjectId ${CMAKE_CURRENT_SOURCE_DIR} NAME)
string(REPLACE " " "_" ProjectId ${ProjectId})

# Set the project name
project(${ProjectId})

# Cache the root directory path
set(ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR} CACHE PATH "Root directory path")

# Add Debug, Release and Deploy configurations
set(CMAKE_CONFIGURATION_TYPES "Debug;Release;Deploy" CACHE STRING "Configs" FORCE)

# Set the build info option to OFF by default and do not cache it.
option(BUILD_INFO "Build info" OFF)

# Set the output directory for the build executables and libraries directly in the root ./build directory without any configuration subdirectory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/../../build)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/../../artifacts)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/../../artifacts)

# Set /std:c++20 for all configurations
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# /Zc:__cplusplus is required to make __cplusplus accurate
# /Zc:__cplusplus is available starting with Visual Studio 2017 version 15.7
# (according to https://learn.microsoft.com/en-us/cpp/build/reference/zc-cplusplus)
# That version is equivalent to _MSC_VER==1914
# (according to https://learn.microsoft.com/en-us/cpp/preprocessor/predefined-macros?view=vs-2019)
# CMake's ${MSVC_VERSION} is equivalent to _MSC_VER
# (according to https://cmake.org/cmake/help/latest/variable/MSVC_VERSION.html#variable:MSVC_VERSION)
if(MSVC AND MSVC_VERSION GREATER_EQUAL 1914)
    add_compile_options(/Zc:__cplusplus)
endif()

# Use fast floating point math
add_compile_options(/fp:fast)

# Do not throw exceptions for floating point errors
add_compile_options(/fp:except-)

# Omit frame pointers
add_compile_options(/Oy)

# Use caret diagnostics format
add_compile_options(/diagnostics:caret)

# Disable type information for all configurations
add_compile_options(/GR-)

# Define the Debug configuration compiler flags such as _DEBUG;BUILD_DEBUG=1;BX_CONFIG_DEBUG=1
set(CMAKE_FLAGS_DEBUG "-D_DEBUG -DBUILD_DEBUG=1 -DBX_CONFIG_DEBUG=1 /ZI")
set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} ${}${CMAKE_FLAGS_DEBUG}")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} ${CMAKE_FLAGS_DEBUG}")

# Disable the security checks (/GS-) for the Release configuration
set(CMAKE_FLAGS_RELEASE "${CMAKE_FLAGS_RELEASE} /GS-")

# Favor speed over size (/Ox) for the Release configuration
set(CMAKE_FLAGS_RELEASE "${CMAKE_FLAGS_RELEASE} /Ox")
set(CMAKE_FLAGS_RELEASE "${CMAKE_FLAGS_RELEASE} /Ot")

# Enable link-time code generation (/GL) for the Release configuration
set(CMAKE_FLAGS_RELEASE "${CMAKE_FLAGS_RELEASE} /GL")

# Generate debug info
set(CMAKE_FLAGS_RELEASE "${CMAKE_FLAGS_RELEASE} /Zi")

# Enable function-level linking (/Gy) for the Release configuration
set(CMAKE_FLAGS_RELEASE "${CMAKE_FLAGS_RELEASE} /Gy")

# Enable inlining (/Ob2) for the Release configuration
set(CMAKE_FLAGS_RELEASE "${CMAKE_FLAGS_RELEASE} /Ob2")

# Enable intrinsic functions (/Oi) for the Release configuration
set(CMAKE_FLAGS_RELEASE "${CMAKE_FLAGS_RELEASE} /Oi")

# Enable string pooling (/GF) for the Release configuration
set(CMAKE_FLAGS_RELEASE "${CMAKE_FLAGS_RELEASE} /GF")

# Enable enhanced instruction set (/arch:AVX2) for the Release configuration
set(CMAKE_FLAGS_RELEASE "${CMAKE_FLAGS_RELEASE} /arch:AVX2")

# Enable multi-processor compilation (/MP) for the Release configuration
set(CMAKE_FLAGS_RELEASE "${CMAKE_FLAGS_RELEASE} /MP")

set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} ${CMAKE_FLAGS_RELEASE}")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} ${CMAKE_FLAGS_RELEASE}")

# The zDeploy config inhirit from the Release config
# Define the zDeploy configuration compiler flags from the release config the following ones are added:
set(CMAKE_FLAGS_DEPLOY 
    "-DNDEBUG \
     -DBX_CONFIG_DEBUG=0 \
     -DBUILD_DEPLOY=1 \
     -DBUILD_RELEASE=0 \
     -DBUILD_DEVELOPMENT=0 \
     -DBUILD_ENABLE_LOG=1 \
     -DBUILD_ENABLE_DEBUG_LOG=0 \
     -DBUILD_ENABLE_ASSERT=0 \
     -DBUILD_ENABLE_ERROR_CONTEXT=1 \
     -DBUILD_ENABLE_PROFILE=0 \
     -DBUILD_ENABLE_MEMORY_CONTEXT=1 \
     -DBUILD_ENABLE_MEMORY_TRACKER=1 \
     -DBUILD_ENABLE_MEMORY_GUARD=1 \
     -DBUILD_ENABLE_MEMORY_STATISTICS=0 \
     -DBUILD_ENABLE_STATIC_HASH_DEBUG=1")
set(CMAKE_C_FLAGS_DEPLOY "$(CMAKE_C_FLAGS_DEPLOY) ${CMAKE_C_FLAGS_RELEASE} ${CMAKE_FLAGS_DEPLOY}")
set(CMAKE_CXX_FLAGS_DEPLOY "$(CMAKE_CXX_FLAGS_DEPLOY) ${CMAKE_CXX_FLAGS_RELEASE} ${CMAKE_FLAGS_DEPLOY}")

# Define the Release configuration compiler flags such as BUILD_RELEASE=1;BUILD_ENABLE_ASSERT=0;BX_CONFIG_DEBUG=0;NDEBUG;
set(CMAKE_FLAGS_RELEASE " \
    -DNDEBUG \
    -DBX_CONFIG_DEBUG=0 \
    -DBUILD_RELEASE=1 \
    -DBUILD_ENABLE_ASSERT=0 \
    -DBUILD_ENABLE_DEBUG_LOG=0 \
    ")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} ${CMAKE_FLAGS_RELEASE}")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} ${CMAKE_FLAGS_RELEASE}")

if (BUILD_INFO)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /VERBOSE")
endif()

# Add /INCREMENTAL to debug linker flags
set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} /INCREMENTAL")

# Enable /LTCG for the Release link configuration
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /LTCG")

# Enable COMDAT Folding
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /OPT:ICF")

# Copy release linker flags to deploy
set(CMAKE_EXE_LINKER_FLAGS_DEPLOY "${CMAKE_EXE_LINKER_FLAGS_DEPLOY} ${CMAKE_EXE_LINKER_FLAGS_RELEASE}")

# Set linker to use the static runtime libraries
foreach(flag_var
        CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE CMAKE_CXX_FLAGS_DEPLOY
        CMAKE_C_FLAGS CMAKE_C_FLAGS_DEBUG CMAKE_C_FLAGS_RELEASE CMAKE_C_FLAGS_DEPLOY)
    if(${flag_var} MATCHES "/MD")
        string(REGEX REPLACE "/MD" "/MT" ${flag_var} "${${flag_var}}")
    endif() 
endforeach()

# Add the following definitions to all configurations
add_definitions(
    -DCURL_STATICLIB 
    -DFOUNDATION_SIZE_REAL=8 
    -DIMGUI_ENABLE_TEST_ENGINE 
    -DIMGUI_DISABLE_OBSOLETE_FUNCTIONS=1 
    -DIMPLOT_DISABLE_OBSOLETE_FUNCTIONS=1 
    -DBGFX_CONFIG_USE_TINYSTL=0)

# Use the UNICODE character set
add_definitions(-DUNICODE -D_UNICODE)

# Add _CRT_SECURE_NO_WARNINGS
add_definitions(-D_CRT_SECURE_NO_WARNINGS)
add_definitions(-D_CRT_NONSTDC_NO_DEPRECATE)

# Add __STDC_FORMAT_MACROS
add_definitions(-D__STDC_FORMAT_MACROS)

# Set the compiler to be /permissive-
add_definitions(-D_PERFECT_FORWARDING=1)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /permissive-")

# Add multithreaded compilation /MP
add_compile_options(/MP)

# Execute batch script to generate the version.h file for each supported platform
if(WIN32)
    execute_process(COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_git_build_info.bat ${CMAKE_CURRENT_SOURCE_DIR})
else()
    execute_process(COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate_git_build_info.sh ${CMAKE_CURRENT_SOURCE_DIR})
endif()

# Print the artifacts/version.git.h file
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/artifacts/version.git.h GIT_VERSION_FILE)
message(STATUS "GIT_VERSION_FILE: ${GIT_VERSION_FILE}")

# Load library project under foundation/
add_subdirectory(${ROOT_DIR}/foundation)

# Load library project under imgui/
add_subdirectory(${ROOT_DIR}/imgui)

# Load library project under external/bx/
add_subdirectory(${ROOT_DIR}/external/bx)

# Load library project under external/astc-encoder/
add_subdirectory(${ROOT_DIR}/external/astc-encoder)

# Load library project under external/bimg/
add_subdirectory(${ROOT_DIR}/external/bimg)

# Load library project under external/bgfx/
add_subdirectory(${ROOT_DIR}/external/bgfx)

# Load main exectuable project under sources/
add_subdirectory(${ROOT_DIR}/sources)
