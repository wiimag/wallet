#
# Copyright 2023 Wiimag Inc. All rights reserved.
# License: https://equals-forty-two.com/LICENSE
#
# Installer executable project.
#

cmake_minimum_required (VERSION 3.0)

include(${SHARED_MODULE_PATH})

# Define project
project(installer)

# Load app source files
file(GLOB INSTALLER_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
)
source_group("installer" FILES ${INSTALLER_SOURCES})

# Load app resource files
file(GLOB RESOURCES_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.rc"
)
source_group("resources" FILES ${RESOURCES_SOURCES})

# Load distribution files
file(GLOB RESOURCES_FILES
    "${ROOT_DIR}/resources/*.ico"
    "${ROOT_DIR}/resources/*.png"
    "${ROOT_DIR}/resources/*.ttf"
)
source_group("bundle" FILES ${RESOURCES_FILES})

#
# Create executable
#

# Optimize for size
if (MSVC)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /Os")
endif()

set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")

if (MSVC)

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

if (MSVC)

     # Create executable 
     add_executable(installer WIN32
        ${INSTALLER_SOURCES}
        ${RESOURCES_FILES}
        ${RESOURCES_SOURCES}
    )

    # Get the three first leter from ProjectId
    string(SUBSTRING ${ProjectId} 0 3 ProjectIdShort)

    # Set the executable name to wallet_setup.exe
    set_target_properties(installer PROPERTIES
        OUTPUT_NAME "setup"
    )

elseif(APPLE)

    # Set ASSETCATALOG_COMPILER_APPICON_NAME = AppIcon;
    set_source_files_properties(${RESOURCES_FILES} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

    # Set app icon
    set_source_files_properties(${ROOT_DIR}/resources/App.icns PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")

    # Create Wallet.app bundle
    add_executable(installer MACOSX_BUNDLE
        ${APP_SOURCES}
        ${RESOURCES_FILES}
        ${RESOURCES_SOURCES}
        ${ROOT_DIR}/resources/App.icns
    )

    # Set the bundle name to Wallet.app
    set_target_properties(installer PROPERTIES
        OUTPUT_NAME "${ProjectId}_installer"
        RESOURCE "${RESOURCES_FILES}"
        MACOSX_BUNDLE TRUE
        MACOSX_RPATH TRUE
        MACOSX_FRAMEWORK_IDENTIFIER "com.wiimag.${ProjectId}.installer"
        MACOSX_BUNDLE_INFO_PLIST ${ROOT_DIR}/resources/osx-info.plist
        XCODE_ATTRIBUTE_LD_RUNPATH_SEARCH_PATHS "@loader_path/Libraries"
        XCODE_ATTRIBUTE_ENABLE_HARDENED_RUNTIME TRUE
        XCODE_ATTRIBUTE_EXECUTABLE_NAME "${ProjectId}_installer"
        XCODE_ATTRIBUTE_PRODUCT_NAME "${ProjectId} Installer"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.wiimag.${ProjectId}.installer"
        XCODE_ATTRIBUTE_PRODUCT_CATEGORY "public.app-category.installation"
        XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME "App"
        XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_LAUNCHIMAGE_NAME "LaunchImage"
    )

else()

    # Throw error platform not supported
    message(FATAL_ERROR "Cannot build installer app: Platform not supported")

endif()

# Set project properties
set_target_properties(installer PROPERTIES FOLDER "tools")

target_set_framework_output_dirs(installer ${ROOT_DIR})

# Add dependencies to other libs
target_link_libraries(installer framework)

