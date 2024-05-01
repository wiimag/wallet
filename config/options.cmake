#
# Copyright 2022-2023 - All rights reserved.
# License: https://wiimag.com/LICENSE
#
# Defines a set of cmake options used for the application framework.
#

cmake_minimum_required (VERSION 3.5)

# Set the build info option to OFF by default.
option(BUILD_INFO "Build info" OFF)

# Set the build localization option to ON by default.
option(BUILD_ENABLE_LOCALIZATION "Enable localization" ON)

# Set the build service executable option to OFF by default.
option(BUILD_SERVICE_EXE "Build service executable" OFF)

# Defines how many threads to use for the query system.
option(BUILD_MAX_QUERY_THREADS "Build max query threads" 4)

# Defines how many threads to use for the job system.
option(BUILD_MAX_JOB_THREADS "Build max job threads" 4)

# Set the build tests option to OFF by default.
option(BUILD_ENABLE_TESTS "Build tests" ON)

# Set the build backend option to OFF by default.
option(BUILD_ENABLE_BACKEND "Build backend" OFF)
