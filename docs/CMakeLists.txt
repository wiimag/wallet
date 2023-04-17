#
# Copyright 2022-2023 - All rights reserved.
# License: https://equals-forty-two.com/LICENSE
#
# Project to host all the markdown files
#

cmake_minimum_required (VERSION 3.0)

include(${SHARED_MODULE_PATH})

# Define project
project(docs)

# Add markdown files to the project
file(GLOB_RECURSE MARKDOWN_FILES
    "${ROOT_DIR}/*.md"
)
source_group(TREE ${ROOT_DIR} FILES ${MARKDOWN_FILES})

# Add the markdown files to the project
add_custom_target(docs SOURCES ${MARKDOWN_FILES})

# Set the folder for the project
set_target_properties(docs PROPERTIES FOLDER "extras")
