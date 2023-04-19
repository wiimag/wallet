#!/bin/bash

#
# Copyright 2022-2023 - All rights reserved.
# License: https://equals-forty-two.com/LICENSE
#
# Define a set of bash functions that are shared between the various scripts
#

# Make sure $CONFIG_DIR is set
if [ -z "$CONFIG_DIR" ]; then
  echo "The CONFIG_DIR variable is not set"
  exit 1
fi

# Determine running platform
PLATFORM_NAME="$(uname -s)"
case "${PLATFORM_NAME}" in
    Linux*)     machine=Linux;;
    Darwin*)    machine=Mac;;
    CYGWIN*)    machine=Cygwin;;
    MINGW*)     machine=MinGw;;   # i.e. running from git bash
    *)          machine="UNKNOWN:${PLATFORM_NAME}"
esac

# Determine if we are running on a mac
if [ $machine = "Mac" ]; then
  PLATFORM_MAC=1
else
  PLATFORM_MAC=0
fi

# Determine if we are running on a linux
if [ $machine = "Linux" ]; then
  PLATFORM_LINUX=1
else
  PLATFORM_LINUX=0
fi

# Determine if we are running on a windows
if [ $machine = "Cygwin" ] || [ $machine = "MinGw" ]; then
  PLATFORM_WINDOWS=1
else
  PLATFORM_WINDOWS=0
fi

# Print global script variables
function print_global_variables() {
  echo "Global variables:"
  echo "SCRIPT_DIR: $SCRIPT_DIR"
  echo "CONFIG_DIR: $CONFIG_DIR"
  echo "PLATFORM_NAME: $PLATFORM_NAME"
  echo "PLATFORM_MAC: $PLATFORM_MAC"
  echo "PLATFORM_LINUX: $PLATFORM_LINUX"
  echo "PLATFORM_WINDOWS: $PLATFORM_WINDOWS"
  echo
}

# Function to check if a string is a number
is_number() { 
  [ "$1" ] && [ -z "${1//[0-9]}" ] ;
}

# Function to convert a path to the platform specific format
function convert_path_to_platform() {
  if [ $PLATFORM_WINDOWS -eq 1 ]; then
    win_path=$(cygpath -w "$1")
    # Replace all \ with / in the path
    echo "${win_path//\\/\/}"
  else
    echo "$1"
  fi
}

# Function to convert a path to a file link
function convert_path_to_file_link() {
  local_path=$(convert_path_to_platform $1)
  if [ $machine = "MinGw" ]; then
    echo "file:///${local_path//\\/\/}"
  else
    echo "$1"
  fi
}

#
# @def build_setting <name>
#
# Function to read a named value from config/build.settings.
# Each line is expected to be in the format: NAME=VALUE
# Example:
#   SHORT_NAME=$(build_setting "PROJECT_ID")
#   echo $SHORT_NAME
#
function build_setting() {
    local name=$1
    local value=$(awk -F "=" "/$name/ {print \$2}" "$CONFIG_DIR/build.settings")
    echo $value
}

#
# @def get_command_line_value <switch>
#
# Function to check if the command line contains a given switch (i.e. --run)
# Example: 
#   if [ $(has_command_line --run) -eq 0 ]; then
#     echo "The --run switch was found"
#   fi
#
function has_command_line_arg() {
  local switch=$1
  local result=1
  for var in "$@"
  do
    # Check if the var start with the switch
    if [[ $var == $switch* ]]; then
      result=0
    # Check if the form is --$var*
    elif [[ $var == "--$var"* ]]; then
      result=0
    elif [[ $var == "-$var"* ]]; then
      result=0
    fi
  done
  echo $result
}
