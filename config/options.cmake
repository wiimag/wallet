#
# Copyright 2023 Wiimag Inc. All rights reserved.
# License: https://equals-forty-two.com/LICENSE
#
# Defines a set of cmake options used for the application framework.
#

cmake_minimum_required (VERSION 3.0)

# Set the build info option to OFF by default and do not cache it.
option(BUILD_INFO "Build info" OFF)

option(BUILD_ENABLE_LOCALIZATION "Enable localization" ON)