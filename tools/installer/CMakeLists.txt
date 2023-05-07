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

# Read PROJECT_ID for the setup.sfx.in file
read_build_settings_property("PROJECT_ID" PROJECT_ID)
string(REPLACE " " "_" PROJECT_ID ${PROJECT_ID})

#
# Configure the WiX installer script
#
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/setup.wxs.in"
    "${CMAKE_CURRENT_SOURCE_DIR}/setup.wxs"
    @ONLY
)

if (WIN32)

    # Get the list of source files
    file(GLOB INSTALLER_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/*.wxs"
        "${CMAKE_CURRENT_SOURCE_DIR}/*.wxs.in"
        "${CMAKE_CURRENT_SOURCE_DIR}/*.png"
    )

    add_custom_target(installer SOURCES ${INSTALLER_SOURCES})
    
    # Set project properties
    set_target_properties(installer PROPERTIES FOLDER "tools")

endif()
