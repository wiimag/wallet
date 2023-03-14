#!/bin/bash
# 
# Zip the build application folder using WinRar
#

# Load the project name
SHORT_NAME=$(basename $(git rev-parse --show-toplevel))

# Load the WinRar path from Program Files
WIN_RAR_EXE_PATH="C:\Program Files\WinRAR\WinRAR.exe"

# Load the build folder path
BUILD_FOLDER_PATH="build"

# Make sure the build folder exists
if [ ! -d "$BUILD_FOLDER_PATH" ]; then
  echo "The build folder does not exist"
  exit 1
fi

# Set the project exe path
PROJECT_EXE_PATH="$BUILD_FOLDER_PATH/$SHORT_NAME.exe"

# Make sure the project exe is in the build folder
if [ ! -f "$PROJECT_EXE_PATH" ]; then
  echo "The project exe does not exist in the build folder"
  exit 1
fi

# Create the artifacts folder if it does not exist
if [ ! -d "artifacts" ]; then
  mkdir artifacts
fi

# Define the zip output path
ZIP_OUTPUT_PATH="artifacts/$BUILD_FOLDER_PATH.zip"

# Execute the WinRar command on the build folder to create a zip file
"$WIN_RAR_EXE_PATH" a -r -ep1 -ibck "$ZIP_OUTPUT_PATH" "$BUILD_FOLDER_PATH"

# Make the zip output path absolute and convert to windows path using cygwin
ZIP_OUTPUT_PATH=$(cygpath -w $(realpath $ZIP_OUTPUT_PATH))

# Convert the zip output path to a file:// url
ZIP_OUTPUT_PATH=${ZIP_OUTPUT_PATH//\\/\/}

# Print the build zip path
echo "Build package: file://$ZIP_OUTPUT_PATH"