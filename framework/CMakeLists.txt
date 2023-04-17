#
# Copyright 2022-2023 - All rights reserved.
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

#
# Load test source files
#
file(GLOB TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.h"
)
source_group("tests" FILES ${TEST_SOURCES})

# Exclude *_tests.cpp files from the framework library as they will be included by the app executable
list(FILTER TEST_SOURCES EXCLUDE REGEX ".*_tests.cpp")

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
    "${ROOT_DIR}/artifacts/version.git.h"
)
source_group("resources" FILES ${DEVS_RESOURCE_FILES})

# Load config files
file(GLOB_RECURSE CONFIG_FILES
    "${ROOT_DIR}/config/*.sjson"
)
set(CONFIG_FILES ${CONFIG_FILES} PARENT_SCOPE)
source_group("config" FILES ${CONFIG_FILES})

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

# Set shaderc executable path
if (MSVC)
    
    set(SHADERC_EXE "${ROOT_DIR}/tools/win64/shaderc.exe" PARENT_SCOPE)

elseif (APPLE)

    set(SHADERC_EXE "${ROOT_DIR}/tools/osx/shaderc" PARENT_SCOPE)

else()

    # Throw an error if the shaderc executable is not found
    message(FATAL_ERROR "shaderc executable not set for this platform")

endif()

#
# Compiler flags
#

# Use compiler flags from parent project
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")

# Use compiler flags from parent project
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")

# Add sources include directories
# FIXME: This is needed for now to access resources files (i.e. #include <resource.h>)
include_directories(${ROOT_DIR}/sources)

# Create the application library
add_library(framework STATIC 
    ${FRAMEWORK_SOURCES} 
    ${TEST_SOURCES} 
    ${EXTERNAL_SOURCES} 
    ${STB_SOURCES} 
    ${DOCTEST_SOURCES} 
    ${CONFIG_FILES}
    ${DEVS_RESOURCE_FILES})

# Add dependencies to other libs
target_link_libraries(framework PUBLIC foundation bx bgfx imgui)
target_include_directories(framework PUBLIC ${ROOT_DIR})
target_include_directories(framework PUBLIC ${ROOT_DIR}/external)
target_compile_definitions(framework PUBLIC BUILD_SERVICE=0)
target_compile_definitions(framework PUBLIC BUILD_APPLICATION=1)

if (BUILD_SERVICE_EXE)

    # Framework service lib variante (for the service.exe executable)
    add_library(service-framework STATIC 
        ${FRAMEWORK_SOURCES} 
        ${EXTERNAL_SOURCES} 
        ${STB_SOURCES})

    target_link_libraries(service-framework PUBLIC foundation bx imgui)
    set_target_properties(service-framework PROPERTIES FOLDER "service/")

    # Add BUILD_SERVICE preprocessor definition
    target_compile_definitions(service-framework PUBLIC BUILD_SERVICE=1)
    target_compile_definitions(service-framework PUBLIC BUILD_APPLICATION=0)
    target_include_directories(service-framework PUBLIC ${ROOT_DIR})
    target_include_directories(service-framework PUBLIC ${ROOT_DIR}/external)

endif()

#
# Set framework and dependencies options
#

if (WIN32)
  
    # Enable OpenMP compiler support /openmp
    target_compile_options(framework PUBLIC /openmp)

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

if (BUILD_SERVICE_EXE)
    target_include_directories(service-framework PUBLIC ${ROOT_DIR}/external/glfw/include)
endif()

if (WIN32)
    
    # On Windows we use a #pragma comment(lib, "glfw3.lib") in the source code
    target_link_directories(framework PUBLIC ${ROOT_DIR}/external/glfw/lib-vc2022-64)

    if (BUILD_SERVICE_EXE)
        target_link_directories(service-framework PUBLIC ${ROOT_DIR}/external/glfw/lib-vc2022-64)
    endif()

elseif(APPLE)

    # Link with GLFW3 and do not add /Debug to -L option
    target_link_directories(framework PUBLIC ${ROOT_DIR}/external/glfw/lib-osx-universal)
    target_link_libraries(framework PUBLIC glfw3)

endif()

#
# Link with libcurl library
#

# Define CURL_STATICLIB to use libcurl static library
target_compile_definitions(framework PUBLIC CURL_STATICLIB)
target_include_directories(framework PUBLIC ${ROOT_DIR}/external/libcurl/include)

if (BUILD_SERVICE_EXE)
    target_compile_definitions(service-framework PUBLIC CURL_STATICLIB)
    target_include_directories(service-framework PUBLIC ${ROOT_DIR}/external/libcurl/include)
endif()

if (MSVC)

    # On Windows we use a #pragma comment(lib, "libcurl_a*.lib") in the source code
    target_link_directories(framework PUBLIC ${ROOT_DIR}/external/libcurl/lib)

    if (BUILD_SERVICE_EXE)
        target_link_directories(service-framework PUBLIC ${ROOT_DIR}/external/libcurl/lib)
    endif()

elseif(APPLE)

    # Link with libcurl system library
    target_link_libraries(framework PUBLIC curl)

endif()
