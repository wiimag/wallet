#
# Copyright 2022-2023 - All rights reserved.
# License: https://equals-forty-two.com/LICENSE
#
# Build the C++ bx library project
#

project(bx)

# bx library directory
set(BX_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# bx library
file(GLOB_RECURSE BX_SOURCES ${BX_DIR}/src/*.cpp)

file(GLOB_RECURSE BX_INCLUDES
    ${BX_DIR}/src/*.h
    ${BX_DIR}/src/*.inl
    ${BX_DIR}/include/bx/*.h
)

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

file(GLOB_RECURSE BX_COMPAT_SOURCES ${BX_COMPAT_DIR}/*.h)

# Add include files under external/ini
file(GLOB BX_INI_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/ini/*.h)

# Create groups for the source files
source_group("bx" FILES ${BX_INCLUDES})
source_group("src" FILES ${BX_SOURCES})
source_group("ini" FILES ${BX_INI_SOURCES})
source_group("compat" FILES ${BX_COMPAT_SOURCES})

# Add _LIB only for static library
add_compile_definitions(_LIB)

# Add the compatibility headers to the include path
include_directories(${BX_COMPAT_DIR})

# Add the compatibility headers to the include path
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/)

# Use compiler flags from parent project
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")

# Use compiler flags from parent project
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")

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

# Create the bx library
add_library(bx STATIC ${BX_INCLUDES} ${BX_SOURCES} ${BX_COMPAT_SOURCES} ${BX_INI_SOURCES})

# Set project properties
set_target_properties(bx PROPERTIES FOLDER "libs/bgfx")

target_include_directories(bx PUBLIC
    ${BX_DIR}/include
    ${BX_COMPAT_DIR}
)

target_compile_definitions(bx PUBLIC
    "$<$<CONFIG:DEBUG>:BX_CONFIG_DEBUG=1>"
    "$<$<CONFIG:RELEASE,DEPLOY>:BX_CONFIG_DEBUG=0>"
)

if (APPLE)

    # Ignore -Wno-shadow for all dependencies
    target_compile_options(bx PUBLIC -Wno-shadow)

endif()
