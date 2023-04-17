#
# Copyright 2022-2023 - All rights reserved.
# License: https://equals-forty-two.com/LICENSE
#
# Main app executable CMakeLists.txt project file.
#

cmake_minimum_required (VERSION 3.0)

include(${SHARED_MODULE_PATH})

# Set project name
set(AppId "${ProjectId}")

# Define project
project(${AppId})

# Load app source files
file(GLOB APP_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.inl"
)
source_group("app" FILES ${APP_SOURCES})

# Load app resource files
file(GLOB RESOURCES_SOURCES
    "${ROOT_DIR}/resources/*.ico"
    "${ROOT_DIR}/resources/*.manifest"
    "${ROOT_DIR}/config/locales.sjson"
    "${ROOT_DIR}/config/build.settings"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.rc"
)
source_group("resources" FILES ${RESOURCES_SOURCES})

# Load distribution files
file(GLOB BUNDLE_FILES
    "${ROOT_DIR}/resources/*.png"
    "${ROOT_DIR}/resources/*.ttf"
    "${ROOT_DIR}/resources/*.plist"
)
source_group("bundle" FILES ${BUNDLE_FILES})

#
# Load app shader files
#
file(GLOB_RECURSE VERTEX_SHADERS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.vsc")
source_group("shaders\\vertex" FILES ${VERTEX_SHADERS})

file(GLOB_RECURSE FRAGMENT_SHADERS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.fsc")
source_group("shaders\\fragment" FILES ${FRAGMENT_SHADERS})

file(GLOB_RECURSE COMMON_SHADERS_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.sc")
source_group("shaders\\common" FILES ${COMMON_SHADERS_SOURCES})

# Set generated shader headers list
SET(GENERATED_SHADER_HEADERS "${GENERATED_SHADER_HEADERS}")

#
# Compile all shaders with shaderc
#

set(SHADER_OUTPUT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders/generated)

# Set the shader suffix backend
if (WIN32)
    set(SHADER_SUFFIX "dx11")
    set(SHADER_PLATFORM "windows")
    set(SHADER_VERTEX_MODEL "vs_5_0")
    set(SHADER_FRAGMENT_MODEL "ps_5_0")
elseif (APPLE)
    set(SHADER_SUFFIX "metal")
    set(SHADER_PLATFORM "osx")
    set(SHADER_VERTEX_MODEL "metal")
    set(SHADER_FRAGMENT_MODEL "metal")
endif()

# Compile all vertex shaders
foreach(VERTEX_SHADER ${VERTEX_SHADERS})

    # Get shader name
    get_filename_component(VERTEX_SHADER_NAME ${VERTEX_SHADER} NAME_WE)

    # Get shader directory
    get_filename_component(VERTEX_SHADER_DIR ${VERTEX_SHADER} DIRECTORY)

    # Set varying.def.sc file path
    set(VARYING_DEF_FILE "${VERTEX_SHADER_DIR}/varying.def.sc")

    # Set output shader file path
    set(VERTEX_SHADER_OUTPUT_H "${SHADER_OUTPUT_DIR}/vs_${VERTEX_SHADER_NAME}.${SHADER_SUFFIX}.h")
    
    add_custom_command(
        OUTPUT ${VERTEX_SHADER_OUTPUT_H}
        COMMAND ${SHADERC_EXE} --type vertex --platform ${SHADER_PLATFORM} -p ${SHADER_VERTEX_MODEL} -O 3 -i "${BGFX_DIR}/src"
            --varyingdef "${VARYING_DEF_FILE}"
            -f ${VERTEX_SHADER} --bin2c vs_${VERTEX_SHADER_NAME}_${SHADER_SUFFIX}
            -o ${VERTEX_SHADER_OUTPUT_H}
        DEPENDS ${VERTEX_SHADER} ${VARYING_DEF_FILE}
    )
    list(APPEND GENERATED_SHADER_HEADERS ${VERTEX_SHADER_OUTPUT_H})
endforeach()

# Compile all fragment shaders
foreach(FRAGMENT_SHADER ${FRAGMENT_SHADERS})

    # Get shader name
    get_filename_component(FRAGMENT_SHADER_NAME ${FRAGMENT_SHADER} NAME_WE)

    # Get shader directory
    get_filename_component(FRAGMENT_SHADER_DIR ${FRAGMENT_SHADER} DIRECTORY)

    # Set varying.def.sc file path
    set(VARYING_DEF_FILE "${FRAGMENT_SHADER_DIR}/varying.def.sc")

    # Set output shader file path
    set(FRAGMENT_SHADER_OUTPUT_H "${SHADER_OUTPUT_DIR}/fs_${FRAGMENT_SHADER_NAME}.${SHADER_SUFFIX}.h")
    
    add_custom_command(
        OUTPUT ${FRAGMENT_SHADER_OUTPUT_H}
        COMMAND ${SHADERC_EXE} --type fragment --platform ${SHADER_PLATFORM} -p ${SHADER_FRAGMENT_MODEL} -O 3 -i "${BGFX_DIR}/src"
            --varyingdef "${VARYING_DEF_FILE}"
            -f ${FRAGMENT_SHADER} --bin2c fs_${FRAGMENT_SHADER_NAME}_${SHADER_SUFFIX}
            -o ${FRAGMENT_SHADER_OUTPUT_H}
        DEPENDS ${FRAGMENT_SHADER} ${VARYING_DEF_FILE}
    )
    list(APPEND GENERATED_SHADER_HEADERS ${FRAGMENT_SHADER_OUTPUT_H})
endforeach()

source_group("shaders\\generated" FILES ${GENERATED_SHADER_HEADERS})
set(SHADERS_SOURCES ${COMMON_SHADERS_SOURCES} ${VERTEX_SHADERS} ${FRAGMENT_SHADERS} ${GENERATED_SHADER_HEADERS})

#
# Load test source files
#
file(GLOB TEST_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp"
    "${ROOT_DIR}/framework/tests/*_tests.cpp"
)
source_group("tests" FILES ${TEST_SOURCES})

# Include self directory for tests sources to access headers as if it was in the same directory
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

#
# Linker flags
#

set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")

if (MSVC)

    # Generate PDB files
    set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} /DEBUG")

    # Ignore specific default libraries msvcrt.lib;libcmt.lib in debug for MSVC
    set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} /NODEFAULTLIB:msvcrt.lib /NODEFAULTLIB:libcmt.lib")

elseif(XCODE)

    # Link with core libraries
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -framework CoreFoundation -framework CoreServices")

    # Link with carbon library
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -framework Carbon")

    # Link with Cocoa library
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -framework Cocoa")

    # Link with IOKit library
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -framework IOKit")

    # Link with Metal library
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -weak_framework Metal -weak_framework MetalKit -framework QuartzCore")

endif()

# Write the executable file to the build folder directly without any configuration sub folder
if (MSVC)

    # Define PRODUCT_WINDOWS_FILENAME
    add_compile_options(-DPRODUCT_WINDOWS_FILENAME="${AppId}.exe")

    # Create executable
    add_executable(${AppId} WIN32
        ${APP_SOURCES}
        ${RESOURCES_SOURCES}
        ${SHADERS_SOURCES}
        ${TEST_SOURCES}
        ${EXTERNAL_SOURCES}
    )

    # Copy the config/*.sjson files to the build directory
    #file(COPY ${CONFIG_FILES} DESTINATION ${ROOT_DIR}/build)

    set_target_properties(${AppId} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${ROOT_DIR}/build/.)
    set_target_properties(${AppId} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_DEBUG ${ROOT_DIR}/build/.)
    set_target_properties(${AppId} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELEASE ${ROOT_DIR}/build/.)
    set_target_properties(${AppId} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_DEPLOY ${ROOT_DIR}/build/.)

    # Make sure we have the pdb in the same folder.
    set_target_properties(${AppId} PROPERTIES PDB_OUTPUT_DIRECTORY ${ROOT_DIR}/build/.)
    set_target_properties(${AppId} PROPERTIES PDB_OUTPUT_DIRECTORY_DEBUG ${ROOT_DIR}/build/.)
    set_target_properties(${AppId} PROPERTIES PDB_OUTPUT_DIRECTORY_RELEASE ${ROOT_DIR}/build/.)
    set_target_properties(${AppId} PROPERTIES PDB_OUTPUT_DIRECTORY_DEPLOY ${ROOT_DIR}/build/.)

    if (BUILD_SERVICE_EXE)
        # Create service.exe target to provide a way to run the app from the command line
        add_executable(service-exe WIN32
            ${APP_SOURCES}
            ${RESOURCES_SOURCES}
            ${EXTERNAL_SOURCES}
        )

        set_target_properties(service-exe PROPERTIES FOLDER "service/")

        # Build subsystem is console
        set_target_properties(service-exe PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE")

        set_target_properties(service-exe PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${ROOT_DIR}/build/.)
        set_target_properties(service-exe PROPERTIES RUNTIME_OUTPUT_DIRECTORY_DEBUG ${ROOT_DIR}/build/.)
        set_target_properties(service-exe PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELEASE ${ROOT_DIR}/build/.)
        set_target_properties(service-exe PROPERTIES RUNTIME_OUTPUT_DIRECTORY_DEPLOY ${ROOT_DIR}/build/.)

        target_link_libraries(service-exe service-framework)

        # Set executable file name to `service.exe`
        set_target_properties(service-exe PROPERTIES OUTPUT_NAME "service")

        # Optimize for size at compile time
        set_target_properties(service-exe PROPERTIES COMPILE_FLAGS "/Os")
    endif()

elseif(APPLE)

    # Set ASSETCATALOG_COMPILER_APPICON_NAME = AppIcon;
    set_source_files_properties(
        ${BUNDLE_FILES} 
        ${CONFIG_FILES} 
        PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

    # Set app icon
    set_source_files_properties(${ROOT_DIR}/resources/App.icns PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")

    # Create application MacOS bundle
    add_executable(${AppId} MACOSX_BUNDLE
        ${APP_SOURCES}
        ${BUNDLE_FILES}
        ${RESOURCES_SOURCES}
        ${SHADERS_SOURCES}
        ${TEST_SOURCES}
        ${EXTERNAL_SOURCES}
        ${CONFIG_FILES} 
        ${ROOT_DIR}/resources/App.icns
    )

    # Convert AppId string to CamelCase
    ucfirst(${AppId} AppIdCamelCase)

    # Set the bundle name and other properties
    set_target_properties(${AppId} PROPERTIES
        OUTPUT_NAME "${AppIdCamelCase}"
        MACOSX_BUNDLE TRUE
        MACOSX_RPATH TRUE
        MACOSX_FRAMEWORK_IDENTIFIER "com.wiimag.${AppId}"
        MACOSX_BUNDLE_INFO_PLIST ${ROOT_DIR}/resources/osx-info.plist
        XCODE_ATTRIBUTE_LD_RUNPATH_SEARCH_PATHS "@loader_path/Libraries"
        RESOURCE "${BUNDLE_FILES}"
        XCODE_ATTRIBUTE_ENABLE_HARDENED_RUNTIME TRUE
        XCODE_ATTRIBUTE_EXECUTABLE_NAME "${AppIdCamelCase}"
        XCODE_ATTRIBUTE_PRODUCT_NAME "${AppIdCamelCase}"
        XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.wiimag.${AppId}"
        XCODE_ATTRIBUTE_PRODUCT_CATEGORY "public.app-category.finance"
        XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME "App"
        XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_LAUNCHIMAGE_NAME "LaunchImage"
    )

    # Create the application bundle under the build/ folder
    set_target_properties(${AppId} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${ROOT_DIR}/build/.)
    set_target_properties(${AppId} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_DEBUG ${ROOT_DIR}/build/.)
    set_target_properties(${AppId} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_RELEASE ${ROOT_DIR}/build/.)
    set_target_properties(${AppId} PROPERTIES RUNTIME_OUTPUT_DIRECTORY_DEPLOY ${ROOT_DIR}/build/.)

endif()

# Add dependencies to other libs
target_link_libraries(${AppId} framework)
