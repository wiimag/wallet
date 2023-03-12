#
# Copyright 2023 Wiimag Inc. All rights reserved.
# License: https://equals-forty-two.com/LICENSE
#
# Build the C++ bx library project
#

project(bx)

# bx library directory
set(BX_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# bx library
file(GLOB_RECURSE BX_SOURCES
    ${BX_DIR}/src/*.cpp
)

file(GLOB_RECURSE BX_INCLUDES
    ${BX_DIR}/include/bx/*.h
    ${BX_DIR}/src/*.h
    ${BX_DIR}/src/*.inl
)

# Create groups for the source files
source_group("bx" FILES ${BX_INCLUDES})
source_group("src" FILES ${BX_SOURCES})

# Include compatibility headers per platform and set include path
if (WIN32)

    set(BX_COMPAT_DIR ${BX_DIR}/include/compat/msvc)

elseif (APPLE)

    # Same for iOS
    if (IOS)
        set(BX_COMPAT_DIR ${BX_DIR}/include/compat/ios)    
    else()
        set(BX_COMPAT_DIR ${BX_DIR}/include/compat/osx)
    endif()

elseif (UNIX)

    set(BX_COMPAT_DIR ${BX_DIR}/include/compat/linux)

endif()

file(GLOB_RECURSE BX_COMPAT_SOURCES
    ${BX_COMPAT_DIR}/*.h
)

# Add the compatibility headers to the include path
include_directories(${BX_COMPAT_DIR})

# Create a group for the compatibility headers
source_group("compat" FILES ${BX_COMPAT_SOURCES})

# Add include files under external/ini
file(GLOB BX_INI_SOURCES
    ${ROOT_DIR}/external/ini/*.h
)

# Add source group
source_group("ini" FILES ${BX_INI_SOURCES})

# Add the compatibility headers to the include path
include_directories(${ROOT_DIR}/external/)

# Add _LIB only for static library
add_compile_definitions(_LIB)

if (MVSC)

    # Disable CPP exceptions
    add_compile_options(/EHsc)

    # Disable RTTI
    add_compile_options(/GR-)

elseif (APPLE)

    # Disable CPP exceptions
    add_compile_options(-fno-exceptions)

    # Disable RTTI
    add_compile_options(-fno-rtti)

endif()

# Use compiler flags from parent project
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")

# Use compiler flags from parent project
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")

# Create the bx library
add_library(bx STATIC ${BX_INCLUDES} ${BX_SOURCES} ${BX_COMPAT_SOURCES} ${BX_INI_SOURCES})

target_include_directories(bx PUBLIC
    ${BX_DIR}/include
    ${BX_COMPAT_DIR}
)

target_compile_definitions(bx PUBLIC
    "$<$<CONFIG:DEBUG>:BX_CONFIG_DEBUG=1>"
    "$<$<CONFIG:RELEASE,DEPLOY>:BX_CONFIG_DEBUG=0>"
)
