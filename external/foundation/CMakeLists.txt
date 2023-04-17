#
# Copyright 2022-2023 - All rights reserved.
# License: https://equals-forty-two.com/LICENSE
#
# Build the C foundation_lib library project
#

# Project name
project(foundation)

# Project source files (all *.c files)
file(GLOB_RECURSE foundation_src "*.c")

# Add *.m files on OSX
if (APPLE)
    file(GLOB_RECURSE foundation_src_osx "*.m")
    list(APPEND foundation_src ${foundation_src_osx})
endif()

# Project header files (all *.h files)
file(GLOB_RECURSE foundation_includes "*.h")

# Add source groups
source_group("src" FILES ${foundation_src})
source_group("includes" FILES ${foundation_includes})

# Add _LIB only for static library
add_compile_definitions(_LIB)

# Use compiler flags from parent project
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")

# Use compiler flags from parent project
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")

# Project library
add_library(foundation STATIC ${foundation_src} ${foundation_includes})

# Add the project under the solution filter dir /libs
set_target_properties(foundation PROPERTIES FOLDER "libs")

# Project include directories
target_include_directories(foundation PUBLIC ${ROOT_DIR}/external/)

# Export a few defines to the project
target_compile_definitions(foundation PUBLIC -DFOUNDATION_SIZE_REAL=8)

if (APPLE)
    # Set Xcode project properties
    set_target_properties(foundation
                          PROPERTIES
                          XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_WEAK YES)
endif()
