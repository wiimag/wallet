#
# Copyright 2023 Wiimag Inc. All rights reserved.
# License: https://equals-forty-two.com/LICENSE
#
# Main app executable CMakeLists.txt project file.
#

cmake_minimum_required (VERSION 3.0)

# Set project name
set(AppId "${ProjectId}")

# Define project
project(${AppId})

set(FrameworkDir "${CMAKE_CURRENT_SOURCE_DIR}/framework")

# Load framework source files
file(GLOB FRAMEWORK_SOURCES
    "${FrameworkDir}/*.cpp"
    "${FrameworkDir}/*.h"
    "${FrameworkDir}/*.hpp"
    "${FrameworkDir}/*.inl"
)

# Add *.mm files for OSX
if (APPLE)
    file(GLOB_RECURSE FRAMEWORK_SOURCES_OSX
        ${BGFX_DIR}/src/*.mm
    )
    list(APPEND FRAMEWORK_SOURCES ${FRAMEWORK_SOURCES_OSX})
endif()


# Define framework source group
source_group("framework" FILES ${FRAMEWORK_SOURCES})

# Add framework include directories
include_directories(${ROOT_DIR}/external/)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# Load some external source files (only at the root of the external folder)
file(GLOB EXTERNAL_SOURCES
    "${ROOT_DIR}/external/*.c"
    "${ROOT_DIR}/external/*.h"
    "${ROOT_DIR}/external/*.cpp"
    "${ROOT_DIR}/external/*.hpp"
    "${ROOT_DIR}/external/*.inl"
)

# Define external source group
source_group("external" FILES ${EXTERNAL_SOURCES})

# Load stb source files
file(GLOB STB_SOURCES
    "${ROOT_DIR}/external/stb/*.c"
    "${ROOT_DIR}/external/stb/*.h"
)

# Define stb source group
source_group("external\\stb" FILES ${STB_SOURCES})

# Load doctest source files
file(GLOB DOCTEST_SOURCES
    "${ROOT_DIR}/external/doctest/*.cpp"
    "${ROOT_DIR}/external/doctest/*.h"
)

# Define doctest source group
source_group("external\\doctest" FILES ${DOCTEST_SOURCES})

# Load app source files
file(GLOB APP_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.inl"
)
source_group("app" FILES ${APP_SOURCES})

# Load app resource files
file(GLOB RESOURCES_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.rc"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.hint"
    "${ROOT_DIR}/resources/*.ico"
    "${ROOT_DIR}/resources/*.manifest"
    "${ROOT_DIR}/resources/*.plist"
    "${ROOT_DIR}/projects/vs2022/*.natvis"
)
source_group("resources" FILES ${RESOURCES_SOURCES})

# Load app shader files
file(GLOB_RECURSE SHADERS_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.*"
)
source_group("shaders" FILES ${SHADERS_SOURCES})

# Load test source files
file(GLOB TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.h"
)
source_group("tests" FILES ${TEST_SOURCES})

# Use linker flags from parent project
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")

# Add GLFW include directories
include_directories(${ROOT_DIR}/external/glfw/include)

# Link with glfw3.libon Windows
if (WIN32)
    link_directories(${ROOT_DIR}/external/glfw/lib-vc2022-64)
endif()

# Add libcurl include directories
include_directories(${ROOT_DIR}/external/libcurl/include)

# Link with libcurl shared framework on OSX
if (APPLE)
    #find_package(CURL CONFIG REQUIRED)
    #target_link_libraries(${AppId} PRIVATE CURL::libcurl)
else()
    # Link with libcurl_a_debug.lib on Windows and in debug mode
    link_directories(${ROOT_DIR}/external/libcurl/lib)
endif()

# Add ignore warning 4244 with MSVC
add_compile_options(/wd4244)

if (MSVC)
    # Generate map files
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /MAP")

    # Generate PDB files
    set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} /DEBUG")
    #set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /DEBUG")

    # Ignore specific default libraries msvcrt.lib;libcmt.lib in debug for MSVC
    set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} /NODEFAULTLIB:msvcrt.lib /NODEFAULTLIB:libcmt.lib")
endif()

# Create executable
add_executable(${AppId} WIN32
    ${FRAMEWORK_SOURCES}
    ${EXTERNAL_SOURCES}
    ${STB_SOURCES}
    ${DOCTEST_SOURCES}
    ${APP_SOURCES}
    ${RESOURCES_SOURCES}
    ${SHADERS_SOURCES}
    ${TEST_SOURCES}
)

# Add dependencies to other libs
target_link_libraries(${AppId} foundation bx bimg bgfx imgui)

# Write the executable file to the build folder directly without any configuration sub folder
if (MSVC)
    set_target_properties(${AppId} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${ROOT_DIR}/build/.)
    set_target_properties(${AppId} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_DEBUG ${ROOT_DIR}/build/.)
    set_target_properties(${AppId} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELEASE ${ROOT_DIR}/build/.)
    set_target_properties(${AppId} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_DEPLOY ${ROOT_DIR}/build/.)

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
endif()

