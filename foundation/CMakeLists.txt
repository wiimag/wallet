#
# Copyright 2023 Wiimag Inc. All rights reserved.
# License: https://equals-forty-two.com/LICENSE
#
# Build the C foundation_lib library project
#

# Project name
project(foundation)

# Project source files (all *.c files)
file(GLOB_RECURSE foundation_src
    "*.c"
)

# Add *.m files on OSX
if (APPLE)
    file(GLOB_RECURSE foundation_src_osx
        "*.m"
    )
    list(APPEND foundation_src ${foundation_src_osx})
endif()

# Project header files (all *.h files)
file(GLOB_RECURSE foundation_includes
    "*.h"
)

# Add source groups
source_group("src" FILES ${foundation_src})
source_group("includes" FILES ${foundation_includes})

# Project include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/../
)

# Use compiler flags from parent project
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")

# Use compiler flags from parent project
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")

# Project library
add_library(foundation STATIC ${foundation_src} ${foundation_includes})

target_include_directories(foundation PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../
)
