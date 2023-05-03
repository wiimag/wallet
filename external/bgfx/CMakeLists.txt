#
# Copyright 2022-2023 - All rights reserved.
# License: https://equals-forty-two.com/LICENSE
#
# Build the C++ bgfx library project
#

project(bgfx)

# bgfx library directory
set(BGFX_DIR ${CMAKE_CURRENT_SOURCE_DIR}/)
set(BGFX_DIR ${BGFX_DIR} PARENT_SCOPE)

# Get bgfx library include files
file(GLOB_RECURSE BGFX_INCLUDES
    ${BGFX_DIR}/include/bgfx/*.h
    ${BGFX_DIR}/src/*.h
    ${BGFX_DIR}/src/*.inl
)

# Get bgfx library source files
file(GLOB_RECURSE BGFX_SOURCES ${BGFX_DIR}/src/*.cpp)

# Add *.mm files for OSX
if (APPLE)
    file(GLOB_RECURSE BGFX_SOURCES_OSX ${BGFX_DIR}/src/*.mm)
    list(APPEND BGFX_SOURCES ${BGFX_SOURCES_OSX})
endif()

# Set source files groups
source_group("src" FILES ${BGFX_SOURCES})
source_group("include" FILES ${BGFX_INCLUDES})

# Add bgfx 3rdparty include dirs
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/khronos/)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/directx-headers/include/directx)

# Use compiler and linker flags from parent project
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")

# Do not use TINYSTL
add_compile_definitions(BGFX_CONFIG_USE_TINYSTL=0)

# Disable vulkan backend renderer for all platforms
add_compile_definitions(BGFX_CONFIG_RENDERER_AGC=0)
add_compile_definitions(BGFX_CONFIG_RENDERER_VULKAN=0)
add_compile_definitions(BGFX_CONFIG_RENDERER_OPENGLES=0)
add_compile_definitions(BGFX_CONFIG_RENDERER_OPENGL=0)

# BGFX tweaks for our needs
add_compile_definitions(BGFX_CONFIG_MAX_SHADERS=16)
add_compile_definitions(BGFX_CONFIG_MAX_VIEW_NAME=16)
add_compile_definitions(BGFX_CONFIG_MAX_DRAW_CALLS=2047)
add_compile_definitions(BGFX_CONFIG_MAX_VERTEX_DECLS=32)
add_compile_definitions(BGFX_CONFIG_MAX_INDEX_BUFFERS=256)
add_compile_definitions(BGFX_CONFIG_MAX_OCCLUSION_QUERIES=8)
add_compile_definitions(BGFX_CONFIG_MAX_DYNAMIC_INDEX_BUFFERS=8192)
add_compile_definitions(BGFX_CONFIG_MAX_DYNAMIC_VERTEX_BUFFERS=8192)

# Disable debug features
add_compile_definitions(BGFX_CONFIG_DEBUG_UNIFORM=0)
add_compile_definitions(BGFX_CONFIG_DEBUG_ANNOTATION=0)
add_compile_definitions(BGFX_CONFIG_DEBUG_OCCLUSION=0)

if (WIN32)

    # Disable CPP exceptions and RTTI
    add_compile_options(/GR-)
    add_compile_options(/EHsc)

    # Enable directx backend renderers
    add_compile_definitions(BGFX_CONFIG_RENDERER_DIRECT3D11=1)
    add_compile_definitions(BGFX_CONFIG_RENDERER_DIRECT3D12=0)

elseif (APPLE)

    # Disable CPP exceptions and RTTI
    add_compile_options(-fno-rtti)
    add_compile_options(-fno-exceptions)

    # Force METAL renderer
    add_compile_definitions(BGFX_CONFIG_RENDERER_METAL=1)

endif()

# Create the bgfx library
add_library(bgfx STATIC ${BGFX_INCLUDES} ${BGFX_SOURCES})

# Set project properties
set_target_properties(bgfx PROPERTIES FOLDER "libs/bgfx")

# Set project dependencies
target_link_libraries(bgfx PUBLIC bx bimg)
target_include_directories(bgfx PUBLIC ${BGFX_DIR}/include)
