#
# License: https://wiimag.com/LICENSE
# Copyright 2022-2023 - All rights reserved.
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

if(APPLE)

    # Choose macOS minimum deployment target to 11.5
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.5" CACHE STRING "Minimum OS X deployment version")

endif()

# Cache the root directory path
set(ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR} CACHE PATH "Root directory path")

# Set build output dir
set(BUILD_DIR ${CMAKE_CURRENT_SOURCE_DIR}/build CACHE PATH "Build directory path")

# Cache the shared module path
# Then each nested CMakeLists.txt can include the shared.cmake file using the following command:
#   include(${SHARED_MODULE_PATH})
set(SHARED_MODULE_PATH ${CMAKE_SOURCE_DIR}/config/shared.cmake CACHE PATH "Shared module path")
include(${SHARED_MODULE_PATH})

# Include the shared options module
set(SHARED_OPTIONS_PATH ${CMAKE_SOURCE_DIR}/config/options.cmake CACHE PATH "Shared options path")
include(${SHARED_OPTIONS_PATH})

# Read the PROJECT_ID=... from the build configs
read_build_settings_property("PROJECT_ID" ProjectId)
string(REPLACE " " "_" ProjectId ${ProjectId})

# Print the project name
message(STATUS "Project: ${ProjectId}")

# Make sure ${ProjectId} can be accessed for all nested projects
set(ProjectId ${ProjectId} CACHE INTERNAL "Set project `${ProjectId}` variable" FORCE)

# Make sure ${ProjectIdCamelCase} can be accessed for all nested projects
ucfirst(${ProjectId} ProjectIdCamelCase)
set(ProjectIdCamelCase ${ProjectIdCamelCase} CACHE INTERNAL "Set project `${ProjectId}` camel case variable" FORCE)

# Set the project name
project(${ProjectId})

# Add Debug, Release and Deploy configurations
set(CMAKE_CONFIGURATION_TYPES "Debug;Release;Deploy" CACHE STRING "Configs" FORCE)

# Generate the version header file
generate_version_git_header(${ROOT_DIR})

# Set the output directory for the build executables and libraries directly in the root ./build directory without any configuration subdirectory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/build)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/artifacts/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/artifacts/lib)

# Set /std:c++20 for all configurations
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# /Zc:__cplusplus is required to make __cplusplus accurate
# /Zc:__cplusplus is available starting with Visual Studio 2017 version 15.7
if(MSVC AND MSVC_VERSION GREATER_EQUAL 1914)
    add_compile_options(/Zc:__cplusplus)
endif()

# Use the UNICODE character set
add_compile_options(-DUNICODE -D_UNICODE)

# Add standard warnings definitions
add_compile_options(-D__STDC_LIMIT_MACROS)
add_compile_options(-D__STDC_FORMAT_MACROS)
add_compile_options(-D__STDC_CONSTANT_MACROS)
add_compile_options(-D_CRT_SECURE_NO_WARNINGS)
add_compile_options(-D_CRT_NONSTDC_NO_DEPRECATE)

# Add _DEBUG compile option for Debug configuration
add_compile_options("$<$<CONFIG:DEBUG>:-D_DEBUG>")

#
# Set common BUILD_* flags
#

# Read project settings to define how many job system threads to use
add_compile_options(-DMAX_JOB_THREADS=${BUILD_MAX_JOB_THREADS})

# Read project settings to define how many query system threads to use
add_compile_options(-DMAX_QUERY_THREADS=${BUILD_MAX_QUERY_THREADS})

add_compile_options("$<$<CONFIG:DEBUG>:-DBUILD_DEBUG=1>")
add_compile_options("$<$<CONFIG:RELEASE>:-DBUILD_RELEASE=1>")
add_compile_options("$<$<CONFIG:DEPLOY>:-DBUILD_DEPLOY=1>")

if (NOT BUILD_ENABLE_DEVELOPMENT)
    add_compile_options("$<$<CONFIG:DEBUG,RELEASE>:-DBUILD_DEVELOPMENT=0>")
endif()
add_compile_options("$<$<CONFIG:DEPLOY>:-DBUILD_DEVELOPMENT=0>")

add_compile_options("$<$<CONFIG:DEBUG,RELEASE,DEPLOY>:-DBUILD_ENABLE_LOG=1>")
add_compile_options("$<$<CONFIG:RELEASE,DEPLOY>:-DBUILD_ENABLE_DEBUG_LOG=0>")
add_compile_options("$<$<CONFIG:RELEASE,DEPLOY>:-DBUILD_ENABLE_ASSERT=0>")
add_compile_options("$<$<CONFIG:DEPLOY>:-DBUILD_ENABLE_ERROR_CONTEXT=1>")
add_compile_options("$<$<CONFIG:DEPLOY>:-DBUILD_ENABLE_PROFILE=0>")
add_compile_options("$<$<CONFIG:DEPLOY>:-DBUILD_ENABLE_MEMORY_CONTEXT=1>")
add_compile_options("$<$<CONFIG:DEPLOY>:-DBUILD_ENABLE_MEMORY_TRACKER=1>")
add_compile_options("$<$<CONFIG:DEPLOY>:-DBUILD_ENABLE_MEMORY_GUARD=1>")
add_compile_options("$<$<CONFIG:DEPLOY>:-DBUILD_ENABLE_MEMORY_STATISTICS=1>")
add_compile_options("$<$<CONFIG:DEPLOY>:-DBUILD_ENABLE_STATIC_HASH_DEBUG=1>")

# Set program information defines
read_build_settings_property("PRODUCT_NAME" PRODUCT_NAME)
add_compile_options(-DPRODUCT_NAME="${PRODUCT_NAME}")

read_build_settings_property("PRODUCT_COMPANY" PRODUCT_COMPANY)
add_compile_options(-DPRODUCT_COMPANY="${PRODUCT_COMPANY}")

read_build_settings_property("PRODUCT_DESCRIPTION" PRODUCT_DESCRIPTION)
add_compile_options(-DPRODUCT_DESCRIPTION="${PRODUCT_DESCRIPTION}")

read_build_settings_property("PRODUCT_COPYRIGHT" PRODUCT_COPYRIGHT)
add_compile_options(-DPRODUCT_COPYRIGHT="${PRODUCT_COPYRIGHT}")

read_build_settings_property("PRODUCT_CODE_NAME" PRODUCT_CODE_NAME)
add_compile_options(-DPRODUCT_CODE_NAME="${PRODUCT_CODE_NAME}")

read_build_settings_property("PRODUCT_URL" PRODUCT_URL)
add_compile_options(-DPRODUCT_URL="${PRODUCT_URL}")

read_build_settings_property("PRODUCT_VERSIONS_URL" PRODUCT_VERSIONS_URL)
add_compile_options(-DPRODUCT_VERSIONS_URL="${PRODUCT_VERSIONS_URL}")

# Set product version defines
read_build_settings_property("VERSION_MAJOR" VERSION_MAJOR)
add_compile_options(-DVERSION_MAJOR=${VERSION_MAJOR})

read_build_settings_property("VERSION_MINOR" VERSION_MINOR)
add_compile_options(-DVERSION_MINOR=${VERSION_MINOR})

read_build_settings_property("VERSION_PATCH" VERSION_PATCH)
add_compile_options(-DVERSION_PATCH=${VERSION_PATCH})

# If the option BUILD_ENABLE_LOCALIZATION is OFF, then disable localization
if(NOT BUILD_ENABLE_LOCALIZATION)
    add_compile_options(-DBUILD_ENABLE_LOCALIZATION=0)
endif()

# Define BUILD_TESTS if BUILD_ENABLE_TESTS is ON
add_compile_options("$<$<CONFIG:DEPLOY>:-DBUILD_TESTS=0>")
if(BUILD_ENABLE_TESTS)
    add_compile_options("$<$<CONFIG:DEBUG,RELEASE>:-DBUILD_TESTS=1>")
else()
    add_compile_options(-DBUILD_TESTS=0)
endif()

#
# Define custom application build target
#
if (BUILD_ENABLE_BACKEND)
    add_compile_options(-DBUILD_BACKEND=1)
else()
    add_compile_options(-DBUILD_BACKEND=0)
endif()

# Set compiler specific flags
if(MSVC)

    # Define _WIN32_WINNT to > Windows 7
    add_compile_options(-D_WIN32_WINNT=0x0601)

    # Enable multi-processor compilation
    add_compile_options(/MP)

    # Add ignore warning 4244 with MSVC
    # warning C4244: 'argument': conversion from 'type1' to 'type2', possible loss of data
    add_compile_options(/wd4244)

    # Use fast floating point math
    add_compile_options(/fp:fast)

    # Do not throw exceptions for floating point errors
    add_compile_options(/fp:except-)

    # Enable enhanced instruction set
    add_compile_options(/arch:AVX2)

    # Omit frame pointers
    add_compile_options(/Oy)

    # Use caret diagnostics format
    add_compile_options(/diagnostics:caret)

    # Disable type information for all configurations
    add_compile_options(/GR-)

    # Generate debug info
    add_compile_options("$<$<CONFIG:DEBUG>:/ZI>")
    add_compile_options("$<$<CONFIG:RELEASE,DEPLOY>:/Zi>")

    # Set the compiler to be /permissive-
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /permissive-")

    # Disable the security checks (/GS-) for the Release configuration
    set(CMAKE_FLAGS_RELEASE "${CMAKE_FLAGS_RELEASE} /GS-")

    # Favor speed over size (/Ox) for the Release configuration
    set(CMAKE_FLAGS_RELEASE "${CMAKE_FLAGS_RELEASE} /Ox /Ot")

    # Enable link-time code generation (/GL) for the Release configuration
    set(CMAKE_FLAGS_RELEASE "${CMAKE_FLAGS_RELEASE} /GL")

    # Enable function-level linking (/Gy) for the Release configuration
    set(CMAKE_FLAGS_RELEASE "${CMAKE_FLAGS_RELEASE} /Gy")

    # Enable inlining (/Ob2) for the Release configuration
    add_compile_options("$<$<CONFIG:DEBUG>:/Ob1>")
    set(CMAKE_FLAGS_RELEASE "${CMAKE_FLAGS_RELEASE} /Ob2")

    # Enable intrinsic functions (/Oi) for the Release configuration
    set(CMAKE_FLAGS_RELEASE "${CMAKE_FLAGS_RELEASE} /Oi")

    # Enable string pooling (/GF) for the Release configuration
    set(CMAKE_FLAGS_RELEASE "${CMAKE_FLAGS_RELEASE} /GF")

    # Set linker to use the static runtime libraries
    foreach(flag_var
        CMAKE_C_FLAGS CMAKE_C_FLAGS_DEBUG CMAKE_C_FLAGS_RELEASE CMAKE_C_FLAGS_DEPLOY
        CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE CMAKE_CXX_FLAGS_DEPLOY)
        if(${flag_var} MATCHES "/MD")
            string(REGEX REPLACE "/MD" "/MT" ${flag_var} "${${flag_var}}")
        endif() 
    endforeach()

    # Add /INCREMENTAL to debug linker flags
    set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} /INCREMENTAL")

    # Enable /LTCG for the Release link configuration
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /LTCG")

    # Enable COMDAT Folding
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /OPT:ICF")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /OPT:REF")

    if (BUILD_INFO)
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /VERBOSE")
    endif()

    # Set /OPT:REF for static libraries as well
    set(CMAKE_STATIC_LINKER_FLAGS_RELEASE "${CMAKE_STATIC_LINKER_FLAGS_RELEASE} /LTCG")

elseif(XCODE)

    # Use fast floating point math
    add_compile_options(-ffast-math)

    # Set preferred warning flags
    add_compile_options(-Wno-switch -Wshadow -Wfatal-errors)

    # Omit frame pointers because they are not needed for debugging
    add_compile_options("$<$<CONFIG:RELEASE,DEPLOY>:-fomit-frame-pointer>")

    # Enable dead code stripping
    add_compile_options(-ffunction-sections -fdata-sections)

    # Enable GCC tuning model for Clang
    add_compile_options(-mtune=native)

    # Allow extern variables to be initialized
    add_compile_options(-Wno-extern-initializer)

    # Add debug info
    add_compile_options("$<$<CONFIG:DEPLOY>:-g0>")
    add_compile_options("$<$<CONFIG:DEPLOY>:-Os>")
    add_compile_options("$<$<CONFIG:RELEASE,DEPLOY>:-O3>")
    add_compile_options("$<$<CONFIG:RELEASE,DEPLOY>:-fvisibility=hidden>")
    add_compile_options("$<$<CONFIG:DEPLOY>:-fvisibility-inlines-hidden>")

    # Add options to get stack traces on crashes
    add_compile_options("$<$<CONFIG:RELEASE,DEPLOY>:-gline-tables-only>")
    add_compile_options("$<$<CONFIG:RELEASE,DEPLOY>:-fomit-frame-pointer>")
    add_compile_options("$<$<CONFIG:RELEASE,DEPLOY>:-foptimize-sibling-calls>")

    # Set tautological-constant-compare and unguarded-availability-new warnings 
    # to be ignored because they are not relevant for us
    if(XCODE_VERSION VERSION_GREATER_EQUAL 10.2)
        add_compile_options(-Wno-tautological-constant-compare -Wno-unguarded-availability-new)
    endif()

    # Define -Wno-varargs
    add_compile_options(-Wno-varargs)

endif()

set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} ${CMAKE_FLAGS_RELEASE}")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} ${CMAKE_FLAGS_RELEASE}")

# The Deploy config inhirit from the Release config
set(CMAKE_C_FLAGS_DEPLOY "${CMAKE_C_FLAGS_DEPLOY} ${CMAKE_C_FLAGS_RELEASE}")
set(CMAKE_CXX_FLAGS_DEPLOY "${CMAKE_CXX_FLAGS_DEPLOY} ${CMAKE_CXX_FLAGS_RELEASE}")

# Copy release linker flags to deploy
set(CMAKE_EXE_LINKER_FLAGS_DEPLOY "${CMAKE_EXE_LINKER_FLAGS_DEPLOY} ${CMAKE_EXE_LINKER_FLAGS_RELEASE}")

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# Load BX library
add_subdirectory(${ROOT_DIR}/external/bx)

# Load BIMG
add_subdirectory(${ROOT_DIR}/external/astc-encoder)
add_subdirectory(${ROOT_DIR}/external/bimg)

# Load BGFX library
add_subdirectory(${ROOT_DIR}/external/bgfx)

# Load library project under foundation/
add_subdirectory(${ROOT_DIR}/external/foundation)

# Load IMGUI library
add_subdirectory(${ROOT_DIR}/external/imgui)

# Load main framework library
add_subdirectory(${ROOT_DIR}/framework)

# Load docs project
add_subdirectory(${ROOT_DIR}/docs)

# Include config/extras.cmake if it exists
if(EXISTS ${ROOT_DIR}/config/extras.cmake)
    include(${ROOT_DIR}/config/extras.cmake)
endif()

# Load main exectuable project under sources/
add_subdirectory(${ROOT_DIR}/sources)

# Add subdirectories for all cmakelist under tools/
file(GLOB_RECURSE CMAKE_LISTS ${ROOT_DIR}/tools/*CMakeLists.txt)
foreach(CMAKE_LIST ${CMAKE_LISTS})
    get_filename_component(CMAKE_LIST_DIR ${CMAKE_LIST} DIRECTORY)
    add_subdirectory(${CMAKE_LIST_DIR})
endforeach()
