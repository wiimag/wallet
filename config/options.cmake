#
# Copyright 2023 Wiimag Inc. All rights reserved.
# License: https://equals-forty-two.com/LICENSE
#
# Defines a set of cmake options used for the application framework.
#

cmake_minimum_required (VERSION 3.0)

# Set the build info option to OFF by default and do not cache it.
option(BUILD_INFO "Build info" OFF)

# Set the build localization option to ON by default and cache it.
option(BUILD_ENABLE_LOCALIZATION "Enable localization" ON)

# Set the build service executable option to OFF by default and do not cache it.
option(BUILD_SERVICE_EXE "Build service executable" OFF)

# Defines how many threads to use for the query system.
option(BUILD_MAX_QUERY_THREADS "Build max query threads" 4)
