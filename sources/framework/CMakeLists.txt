#
# Copyright 2023 Wiimag Inc. All rights reserved.
# License: https://equals-forty-two.com/LICENSE
#
# Application framework library project
#

cmake_minimum_required (VERSION 3.0)

# Define library project
project(framework)

set(FRAMEWORK_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

# Load framework source files
file(GLOB FRAMEWORK_SOURCES
    "${FRAMEWORK_DIR}/*.cpp"
    "${FRAMEWORK_DIR}/*.h"
    "${FRAMEWORK_DIR}/*.hpp"
    "${FRAMEWORK_DIR}/*.inl"
)
source_group("framework" FILES ${FRAMEWORK_SOURCES})

# Add *.mm files for OSX
if (APPLE)
    file(GLOB FRAMEWORK_SOURCES_OSX
        ${FRAMEWORK_DIR}/*.mm
    )
    list(APPEND FRAMEWORK_SOURCES ${FRAMEWORK_SOURCES_OSX})
endif()

# Add framework include directories
include_directories(${ROOT_DIR}/external)

# Load some external source files (only at the root of the external folder)
file(GLOB EXTERNAL_SOURCES
    "${ROOT_DIR}/external/*.c"
    "${ROOT_DIR}/external/*.h"
    "${ROOT_DIR}/external/*.cpp"
    "${ROOT_DIR}/external/*.inl"
)
source_group("external" FILES ${EXTERNAL_SOURCES})

# Load all dev resources files 
file(GLOB_RECURSE DEVS_RESOURCE_FILES 
    "${ROOT_DIR}/*.hint"
    "${ROOT_DIR}/*.natvis"
    "${ROOT_DIR}/*.editorconfig"
)
source_group("resources" FILES ${DEVS_RESOURCE_FILES})

# Load stb source files
file(GLOB STB_SOURCES
    "${ROOT_DIR}/external/stb/*.c"
    "${ROOT_DIR}/external/stb/*.h"
)
source_group("external\\stb" FILES ${STB_SOURCES})

# Load doctest source files
file(GLOB DOCTEST_SOURCES
    "${ROOT_DIR}/external/doctest/*.cpp"
    "${ROOT_DIR}/external/doctest/*.h"
)
source_group("external\\doctest" FILES ${DOCTEST_SOURCES})

#
# Compiler flags
#

# Use compiler flags from parent project
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")

# Use compiler flags from parent project
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")

# Add sources include directories
include_directories(${ROOT_DIR}/sources)

# Create the bx library
add_library(framework STATIC ${FRAMEWORK_SOURCES} ${EXTERNAL_SOURCES} ${STB_SOURCES} ${DOCTEST_SOURCES} ${DEVS_RESOURCE_FILES})

# Add dependencies to other libs
target_link_libraries(framework PUBLIC foundation bx bgfx imgui)

target_include_directories(framework PUBLIC ${ROOT_DIR}/external)

#
# Set framework and dependencies options
#

if (WIN32)
  
    # Enable openmp support
    target_compile_definitions(framework PUBLIC _OPENMP)

    # Enable /Qpar support
    target_compile_options(framework PUBLIC /Qpar)
    target_compile_options(framework PUBLIC /Qpar-report:1)

elseif(APPLE)

    # Enable openmp support
    target_compile_definitions(framework PUBLIC _OPENMP)

endif()

#
# Link with GLFW
#

target_include_directories(framework PUBLIC ${ROOT_DIR}/external/glfw/include)

if (WIN32)
    
    # On Windows we use a #pragma comment(lib, "glfw3.lib") in the source code
    target_link_directories(framework PUBLIC ${ROOT_DIR}/external/glfw/lib-vc2022-64)

elseif(APPLE)

    # Link with libglfw3.a
    #set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -L${ROOT_DIR}/external/glfw/lib-osx-universal -lglfw3")
    target_link_directories(framework PUBLIC ${ROOT_DIR}/external/glfw/lib-osx-universal)
    target_link_libraries(framework PUBLIC glfw3)

endif()

#
# Link with libcurl library
#

# Define CURL_STATICLIB to use libcurl static library
target_compile_definitions(framework PUBLIC CURL_STATICLIB)
target_include_directories(framework PUBLIC ${ROOT_DIR}/external/libcurl/include)

if (MSVC)

    # On Windows we use a #pragma comment(lib, "libcurl_a*.lib") in the source code
    target_link_directories(framework PUBLIC ${ROOT_DIR}/external/libcurl/lib)

elseif(APPLE)

    # Link with libcurl system library
    target_link_libraries(framework PUBLIC curl)

endif()
