#
# Copyright 2022-2023 - All rights reserved.
# License: https://equals-forty-two.com/LICENSE
#
# Build the C++ bimg library project
#

project(bimg)

# bimg library directory
set(BIMG_DIR ${CMAKE_CURRENT_SOURCE_DIR}/)

# bx library
file(GLOB_RECURSE BIMG_SOURCES
    ${BIMG_DIR}/include/bimg/*.h
    ${BIMG_DIR}/src/*.cpp
    ${BIMG_DIR}/src/*.h
    ${BIMG_DIR}/src/*.inl
)

# Exclude image_decode.cpp and image_encode.cpp from the build
list(REMOVE_ITEM BIMG_SOURCES ${BIMG_DIR}/src/image_decode.cpp)
list(REMOVE_ITEM BIMG_SOURCES ${BIMG_DIR}/src/image_encode.cpp)

# Create groups for the source files
source_group("bimg" FILES ${BX_SOURCES})

# Add the compatibility headers to the include path
include_directories(${BIMG_DIR}/include)
include_directories(${ROOT_DIR}/external/)

# Use compiler flags from parent project
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")

# Use compiler flags from parent project
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")

# Create the bx library
add_library(bimg STATIC ${BIMG_SOURCES})

# Add the project under the solution filter dir /libs/bgfx
set_target_properties(bimg PROPERTIES FOLDER "libs/bgfx")

# Depends on bx library
target_link_libraries(bimg bx astc-encoder)
target_include_directories(bimg PUBLIC ${BIMG_DIR}/include)
