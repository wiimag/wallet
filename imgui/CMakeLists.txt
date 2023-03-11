#
# Copyright 2023 Wiimag Inc. All rights reserved.
# License: https://equals-forty-two.com/LICENSE
#
# Build the C++ IMGUI library project
#

project(imgui VERSION 1.89.2)

# Project source files (all *.c files)
file(GLOB_RECURSE imgui_src
    "*.cpp"
)
source_group("src" FILES ${imgui_src})

# Project header files (all *.h files)
file(GLOB_RECURSE imgui_includes
    "*.h"
)
source_group("includes" FILES ${imgui_includes})

# Add .natvis file to the project (include all sub directories)
file(GLOB_RECURSE imgui_natvis
    "*.natvis"
)
source_group("natvis" FILES ${imgui_natvis})

# Project include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/../
)

# Use compiler flags from parent project
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")

# Use compiler flags from parent project
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")

# Use linker flags from parent project
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")

# Use linker flags from parent project
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS}")

# Use linker flags from parent project
set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS}")

# Add the define IMGUI_DISABLE_DEFAULT_ALLOCATORS=1
add_definitions(-DIMGUI_DISABLE_DEFAULT_ALLOCATORS=1)

# Ignore standard warnings (i.e. C4996)
add_definitions(-D_CRT_SECURE_NO_WARNINGS)

# Create the library
add_library(imgui STATIC ${imgui_src} ${imgui_includes} ${imgui_natvis})

# Depends on foundation lib project
add_dependencies(imgui foundation)

target_include_directories(imgui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../
)
