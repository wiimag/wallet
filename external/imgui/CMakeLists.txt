#
# Copyright 2022-2023 - All rights reserved.
# License: https://wiimag.com/LICENSE
#
# Build the C++ IMGUI library project
#

project(imgui VERSION 1.89.5)

# Project source files (all *.c files)
file(GLOB imgui_src "*.cpp")
source_group("src" FILES ${imgui_src})

# Project header files (all *.h files)
file(GLOB_RECURSE imgui_includes "*.h")
source_group("includes" FILES ${imgui_includes})

# Add _LIB only for static library
add_compile_definitions(_LIB)

# Use compiler flags from parent project
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")

# Use compiler flags from parent project
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")

# Create the library
add_library(imgui STATIC ${imgui_src} ${imgui_includes})

# Depends on foundation lib project
add_dependencies(imgui foundation)

# Add the project under the solution filter dir /libs
set_target_properties(imgui PROPERTIES FOLDER "libs")

target_include_directories(imgui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../)

target_compile_definitions(imgui PUBLIC IMGUI_ENABLE_TEST_ENGINE=1)
target_compile_definitions(imgui PUBLIC IMGUI_DISABLE_DEFAULT_ALLOCATORS=1)
target_compile_definitions(imgui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS=1)
target_compile_definitions(imgui PUBLIC IMPLOT_DISABLE_OBSOLETE_FUNCTIONS=1)
target_compile_definitions(imgui PUBLIC IMGUI_DEFINE_MATH_OPERATORS=1)
