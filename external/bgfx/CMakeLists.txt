#
# Copyright 2023 Wiimag Inc. All rights reserved.
# License: https://equals-forty-two.com/LICENSE
#
# Build the C++ bgfx library project
#

project(bgfx)

# bgfx library directory
set(BGFX_DIR ${CMAKE_CURRENT_SOURCE_DIR}/)

# Get bgfx library include files
file(GLOB_RECURSE BGFX_INCLUDES
    ${BGFX_DIR}/include/bgfx/*.h
    ${BGFX_DIR}/src/*.h
    ${BGFX_DIR}/src/*.inl
)

# Get bgfx library source files
file(GLOB_RECURSE BGFX_SOURCES
    ${BGFX_DIR}/src/*.cpp
)

# Add *.mm files for OSX
if (APPLE)
    file(GLOB_RECURSE BGFX_SOURCES_OSX
        ${BGFX_DIR}/src/*.mm
    )
    list(APPEND BGFX_SOURCES ${BGFX_SOURCES_OSX})
endif()

# Create group for the include files
source_group("include" FILES ${BGFX_INCLUDES})

# Create group for the source files
source_group("src" FILES ${BGFX_SOURCES})

# Add the compatibility headers to the include path
include_directories(${ROOT_DIR}/external/)

# Add khronos headers to the include path
include_directories(${ROOT_DIR}/external/khronos/)

# Add directx headers to the include path
include_directories(${ROOT_DIR}/external/directx-headers/include/directx)

# Disable vulkan backend renderer
add_compile_definitions(BGFX_CONFIG_RENDERER_VULKAN=0)

# Do not use TINYSTL
add_compile_definitions(BGFX_CONFIG_USE_TINYSTL=0)

if (WIN32)

    # Disable CPP exceptions
    add_compile_options(/EHsc)

    # Disable RTTI
    add_compile_options(/GR-)

    # Enable directx backend renderers
    add_compile_definitions(BGFX_CONFIG_RENDERER_DIRECT3D11=1)
    add_compile_definitions(BGFX_CONFIG_RENDERER_DIRECT3D12=1)

elseif (APPLE)

    # Disable CPP exceptions
    add_compile_options(-fno-exceptions)

    # Disable RTTI
   add_compile_options(-fno-rtti)
 
    # Force METAL renderer with BGFX_CONFIG_RENDERER_METAL=1
    add_compile_definitions(BGFX_CONFIG_RENDERER_METAL=1)

endif()

# Use compiler and linker flags from parent project
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")

# Create the bgfx library
add_library(bgfx STATIC ${BGFX_INCLUDES} ${BGFX_SOURCES})

target_include_directories(bgfx PUBLIC
    ${BGFX_DIR}/include
)

# Depends on bx library
target_link_libraries(bgfx PUBLIC bx bimg)
