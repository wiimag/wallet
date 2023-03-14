#
# Copyright 2023 Wiimag Inc. All rights reserved.
# License: https://equals-forty-two.com/LICENSE
#
# Main app executable CMakeLists.txt project file.
#

cmake_minimum_required (VERSION 3.0)

include(${SHARED_MODULE_PATH})

# Set project name
set(AppId "${ProjectId}")

# Define project
project(${AppId})

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
)
source_group("resources" FILES ${RESOURCES_SOURCES})

# Load distribution files
file(GLOB RESOURCES_FILES
    "${ROOT_DIR}/resources/*.ico"
    "${ROOT_DIR}/resources/*.png"
    "${ROOT_DIR}/resources/*.ttf"
    "${ROOT_DIR}/resources/*.plist"
)
source_group("bundle" FILES ${RESOURCES_FILES})

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

# Include self directory for tests sources to access headers as if it was in the same directory
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

#
# Linker flags
#

set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")

if (MSVC)

    # Generate map files
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /MAP")

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
        XCODE_ATTRIBUTE_EXECUTABLE_NAME "${AppIdCamelCase}"
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
target_link_libraries(${AppId} framework)
