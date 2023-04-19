#!/bin/bash
# 
# Zip the build application folder using WinRar
#

# Set the directory containing additional scripts
SCRIPT_DIR=$(cd $(dirname $0)/../scripts; pwd)

# Set the directory where the build configuration files are located.
CONFIG_DIR=$(cd $(dirname $0)/../config; pwd)

# Load the scripts/common.sh file
source $SCRIPT_DIR/common.sh

# Load the project name
SHORT_NAME=$(build_setting "PROJECT_ID")

# Get branch current branch name and replace / with _
BRANCH_NAME=$(git rev-parse --abbrev-ref HEAD)
BRANCH_NAME=${BRANCH_NAME//\//_}

# If branch name is empty, use setup
if [ -z "$BRANCH_NAME" ]; then
  BRANCH_NAME="setup"
fi

# If branch name is main, rename to setup
if [ "$BRANCH_NAME" = "main" ]; then
  BRANCH_NAME="release"
fi

# Load the WinRar path from Program Files
WIN_RAR_EXE_PATH="C:\Program Files\WinRAR\WinRAR.exe"

# Load the build folder path
BUILD_FOLDER_PATH="build"

# Make sure the build folder exists
if [ ! -d "$BUILD_FOLDER_PATH" ]; then
  echo "The build folder does not exist"
  exit 1
fi

# Generate and build the solution in depoy mode before packing
if [[ "$*" == *-build* ]]; then
  # Check run script is available
  if [ ! -f "./run" ]; then
    echo "The \`run\` script does not exist, make sure you run this script in the root folder of the project."
    exit 1
  fi

  # Check if deploy is in the command line
  if [[ "$*" == *deploy* ]]; then
    ./run build deploy generate -DBUILD_ENABLE_TESTS=OFF
  else
    ./run build generate -DBUILD_ENABLE_TESTS=OFF
  fi  
fi

# Set the project exe path
PROJECT_EXE_PATH="$BUILD_FOLDER_PATH/$SHORT_NAME.exe"

# Make sure the project exe is in the build folder
if [ ! -f "$PROJECT_EXE_PATH" ]; then
  echo "The project exe ${PROJECT_EXE_PATH} does not exist in the build folder"
  exit 1
fi

# Create the artifacts folder if it does not exist
if [ ! -d "releases" ]; then
  mkdir releases
fi

# Define today's date as YYYY_MM_DD
TODAY=$(date +"%Y_%m_%d")

# Define the zip output path
ZIP_OUTPUT_PATH="releases/${SHORT_NAME}_${BRANCH_NAME}_${TODAY}.exe"

# Define files to be packaged
FILES_TO_PACKAGE=()

# Add all *.exe under the build folder
FILES_TO_PACKAGE+=("$BUILD_FOLDER_PATH/*.exe")

# Add all *.dll under the build folder
FILES_TO_PACKAGE+=("$BUILD_FOLDER_PATH/*.dll")

# Print the files to be packaged
echo "Files to be packaged:"
for file in "${FILES_TO_PACKAGE[@]}"; do
  echo "  $file"
done

# Define sfx banner low and high res
SFX_BANNER_LOW_RES="resources/banner_low_93_302.png"
SFX_BANNER_HIGH_RES="resources/banner_high_186_604.png"

# Define license file path
LICENSE_FILE_PATH="resources/license.txt"

# Define package icon
PACKAGE_ICON="resources/main.ico"

# Define SFX setup script
SFX_SETUP_SCRIPT="resources/setup.sfx"

# Check if SFX config files exist
if [ ! -f "$SFX_SETUP_SCRIPT" ]; then
  echo "The SFX setup script does not exist, it can be generated with \`./run generate\`"
  exit 1
fi

# Delete any existing zip file
if [ -f "$ZIP_OUTPUT_PATH" ]; then
  rm "$ZIP_OUTPUT_PATH" || exit 1
fi

# Execute the WinRar to build a sfx zip file
# SFX: https://techshelps.github.io/WinRAR/html/HELPSFXAdvOpt.htm
# Command line switches: https://techshelps.github.io/WinRAR/html/HELPSwitches.htm
#
"$WIN_RAR_EXE_PATH" \
  a -sfx -z"$SFX_SETUP_SCRIPT" \
  -iicon"$PACKAGE_ICON" -iimg"$SFX_BANNER_LOW_RES" -iimg"$SFX_BANNER_HIGH_RES" \
  -r -ep1 -m5 -ed -cfg- \
  -mt4 -x*.pdb -xsetup.exe \
  "$ZIP_OUTPUT_PATH" "${FILES_TO_PACKAGE[@]}"

# Check if the zip file was created
if [ ! -f "$ZIP_OUTPUT_PATH" ]; then
  echo "The zip file was not created"
  exit 1
fi

# Make the zip output path absolute and convert to windows path using cygwin
ZIP_OUTPUT_PATH=$(cygpath -w $(realpath $ZIP_OUTPUT_PATH))

# Convert the zip output path to a file:// url
ZIP_OUTPUT_PATH=${ZIP_OUTPUT_PATH//\\/\/}

# Print the build zip path
echo "Build package: file://$ZIP_OUTPUT_PATH"

# Copy zip file to default .exe
cp "$ZIP_OUTPUT_PATH" "releases/${SHORT_NAME}_release_latest.exe" 2>/dev/null

# If command line has --run, run the produced exe
if [[ "$*" == *-run* ]]; then
  echo "Running $ZIP_OUTPUT_PATH"
  "$ZIP_OUTPUT_PATH"
fi
