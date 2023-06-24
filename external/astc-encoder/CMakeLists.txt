#
# Copyright 2022-2023 - All rights reserved.
# License: https://wiimag.com/LICENSE
#
# Build the C++ bimg library project
#

project(astc-encoder)

# bimg library directory
set(ASTC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/)

# bx library
file(GLOB_RECURSE ASTC_SOURCES
    ${ASTC_DIR}/include/*.h
    ${ASTC_DIR}/source/*.cpp
    ${ASTC_DIR}/source/*.h
    ${ASTC_DIR}/source/*.inl
)

# Create groups for the source files
source_group("astc-encoder" FILES ${ASTC_SOURCES})

# Add the compatibility headers to the include path
include_directories(${ASTC_SOURCES}/include)
include_directories(${ROOT_DIR}/external/)

# Use compiler flags from parent project
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")

# Use compiler flags from parent project
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")

# Use linker flags from parent project
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS}")

# Use linker flags from parent project
set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS}")

# Create the bx library
add_library(astc-encoder STATIC ${ASTC_SOURCES})

# Set project properties
set_target_properties(astc-encoder PROPERTIES FOLDER "libs/bgfx")

target_include_directories(astc-encoder PUBLIC ${ASTC_DIR}/include)
