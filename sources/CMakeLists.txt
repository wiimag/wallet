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

#
# Shared functions
#

# Define a function to make the first letter of a string uppercase
function(ucfirst str out)
    string(SUBSTRING ${str} 0 1 first)
    string(TOUPPER ${first} first)
    string(SUBSTRING ${str} 1 -1 rest)
    set(${out} "${first}${rest}" PARENT_SCOPE)
endfunction()

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

# Load distribution files
file(GLOB RESOURCES_FILES
    "${ROOT_DIR}/resources/*.ico"
    "${ROOT_DIR}/resources/*.png"
    "${ROOT_DIR}/resources/*.ttf"
    "${ROOT_DIR}/resources/*.plist"
)
source_group("distrib" FILES ${RESOURCES_FILES})

#
# Load app shader files
#
file(GLOB_RECURSE SHADERS_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.*"
)
source_group("shaders" FILES ${SHADERS_SOURCES})

#
# Load test source files
#
file(GLOB TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.h"
)
source_group("tests" FILES ${TEST_SOURCES})

#
# Compiler flags
#

# Add GLFW include directories
include_directories(${ROOT_DIR}/external/glfw/include)

# Add libcurl include directories
include_directories(${ROOT_DIR}/external/libcurl/include)

#
# Linker flags
#

set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")

#
# Link with GLFW
#
if (WIN32)
    
    # On Windows we use a #pragma comment(lib, "glfw3.lib") in the source code
    link_directories(${ROOT_DIR}/external/glfw/lib-vc2022-64)

elseif(APPLE)

    # Link with libglfw3.a
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -L${ROOT_DIR}/external/glfw/lib-osx-universal -lglfw3")

endif()

#
# Link with libcurl library
#

# Define CURL_STATICLIB to use libcurl static library
add_compile_definitions(CURL_STATICLIB)

if (MSVC)

    # On Windows we use a #pragma comment(lib, "libcurl_a*.lib") in the source code
    link_directories(${ROOT_DIR}/external/libcurl/lib)

elseif(APPLE)

    # Link with libcurl system library
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -lcurl")

endif()

if (MSVC)

    # Generate map files
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /MAP")

    # Generate PDB files
    set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} /DEBUG")

    # Ignore specific default libraries msvcrt.lib;libcmt.lib in debug for MSVC
    set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} /NODEFAULTLIB:msvcrt.lib /NODEFAULTLIB:libcmt.lib")

elseif(XCODE)

    # Link with core libraries
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -framework CoreFoundation -framework CoreServices")

    # Link with carbon library
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -framework Carbon")

    # Link with Cocoa library
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -framework Cocoa")

    # Link with IOKit library
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -framework IOKit")

    # Link with Metal library
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -weak_framework Metal -weak_framework MetalKit -framework QuartzCore")

endif()

# Write the executable file to the build folder directly without any configuration sub folder
if (MSVC)
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

elseif(APPLE)

    # Set ASSETCATALOG_COMPILER_APPICON_NAME = AppIcon;
    set_source_files_properties(${RESOURCES_FILES} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

    # Set app icon
    set_source_files_properties(${ROOT_DIR}/resources/App.icns PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")


    # Create Wallet.app bundle
    add_executable(${AppId} MACOSX_BUNDLE
        ${FRAMEWORK_SOURCES}
        ${EXTERNAL_SOURCES}
        ${STB_SOURCES}
        ${DOCTEST_SOURCES}
        ${APP_SOURCES}
        ${RESOURCES_FILES}
        ${RESOURCES_SOURCES}
        ${SHADERS_SOURCES}
        ${TEST_SOURCES}
        ${ROOT_DIR}/resources/App.icns
    )

    # Convert AppId string to CamelCase
    ucfirst(${AppId} AppIdCamelCase)

    # Set the bundle name to Wallet.app
    set_target_properties(${AppId} PROPERTIES
        OUTPUT_NAME "${AppIdCamelCase}"
        MACOSX_BUNDLE TRUE
        MACOSX_RPATH TRUE
        MACOSX_FRAMEWORK_IDENTIFIER "com.wiimag.${AppId}"
        MACOSX_BUNDLE_INFO_PLIST ${ROOT_DIR}/resources/osx-info.plist
        XCODE_ATTRIBUTE_LD_RUNPATH_SEARCH_PATHS "@loader_path/Libraries"
        RESOURCE "${RESOURCES_FILES}"
        XCODE_ATTRIBUTE_ENABLE_HARDENED_RUNTIME TRUE
        XCODE_ATTRIBUTE_EXECUTABLE_NAME "${AppId}"
        XCODE_ATTRIBUTE_PRODUCT_NAME "${AppIdCamelCase}"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.wiimag.${AppId}"
        XCODE_ATTRIBUTE_PRODUCT_CATEGORY "public.app-category.finance"
        XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME "App"
        XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_LAUNCHIMAGE_NAME "LaunchImage"
    )

    # Create Wallet.app under build folder
    set_target_properties(${AppId} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${ROOT_DIR}/build/.)
    set_target_properties(${AppId} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_DEBUG ${ROOT_DIR}/build/.)
    set_target_properties(${AppId} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELEASE ${ROOT_DIR}/build/.)
    set_target_properties(${AppId} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_DEPLOY ${ROOT_DIR}/build/.)

endif()

# Add dependencies to other libs
target_link_libraries(${AppId} foundation bx bimg bgfx imgui)

