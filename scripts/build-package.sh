#!/bin/bash
# 
# Copyright 2023 Wiimag Inc. All rights reserved.
# License: https://equals-forty-two.com/LICENSE
#
# Build the application installation package using WiX
#

# Set the directory containing additional scripts
SCRIPT_DIR=$(cd $(dirname $0)/../scripts; pwd)

# Set the directory where the build configuration files are located.
CONFIG_DIR=$(cd $(dirname $0)/../config; pwd)

# Load the scripts/common.sh file
source $SCRIPT_DIR/common.sh

# Load the project name
SHORT_NAME=$(build_setting "PROJECT_ID")

# Load version numbers
VERSION_MAJOR=$(build_setting "VERSION_MAJOR")
VERSION_MINOR=$(build_setting "VERSION_MINOR")
VERSION_PATCH=$(build_setting "VERSION_PATCH")

# Define today's date as YYYY_MM_DD
TODAY=$(date +"%Y_%m_%d")

# Get branch current branch name and replace / with _
BRANCH_NAME=$(git rev-parse --abbrev-ref HEAD)
BRANCH_NAME=${BRANCH_NAME//\//_}

# If branch name is empty, use default
if [ -z "$BRANCH_NAME" ]; then
  BRANCH_NAME="release"
fi

# If branch name is main, rename to default
if [ "$BRANCH_NAME" = "main" ]; then
  BRANCH_NAME="release"
fi

# Set the WiX binary paths
if [ -z "$WIX_BIN_DIR" ]; then
  WIX_BIN_DIR="C:\Tools\wix311-binaries"
fi
if [ -z "$WIX_CANDLE_EXE_PATH" ]; then
  WIX_CANDLE_EXE_PATH="$WIX_BIN_DIR\candle.exe"
fi
if [ -z "$WIX_LIGHT_EXE_PATH" ]; then
  WIX_LIGHT_EXE_PATH="$WIX_BIN_DIR\light.exe"
fi

# Make sure WiX candle and light are available
if [ ! -f "$WIX_CANDLE_EXE_PATH" ]; then
  echo "The WiX candle.exe exectable cannot be found"
  exit 1
elif [ ! -f "$WIX_LIGHT_EXE_PATH" ]; then
  echo "The WiX light.exe executable cannot be found"
  exit 1
fi

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

  # Set default build options when building the package.
  BUILD_OPTIONS=()
  BUILD_OPTIONS+=("-DBUILD_ENABLE_TESTS=OFF")
  BUILD_OPTIONS+=("-DBUILD_ENABLE_DEVELOPMENT=OFF")

  # Print build options
  echo "Build options:"
  for option in "${BUILD_OPTIONS[@]}"; do
    echo "  $option"
  done

  # Check if deploy is in the command line, if so build accordingly
  if [[ "$*" == *deploy* ]]; then
    ./run build deploy generate ${BUILD_OPTIONS[@]}
  else
    ./run build generate  ${BUILD_OPTIONS[@]}
  fi  
fi

# Set the project exe path
PROJECT_EXE_PATH="$BUILD_FOLDER_PATH/$SHORT_NAME.exe"

# Make sure the project exe is in the build folder
if [ ! -f "$PROJECT_EXE_PATH" ]; then
  echo "The project exe ${PROJECT_EXE_PATH} does not exist in the build folder"
  echo "Make sure you the project was built successfully before running this script"
  exit 1
fi

# Create the artifacts folder if it does not exist
if [ ! -d "releases" ]; then
  mkdir releases
fi

# Define SFX setup script
WIX_SETUP_SCRIPT="tools/installer/setup.wxs"
if [ ! -f "$WIX_SETUP_SCRIPT" ]; then
  echo "The WiX setup script does not exist, it can be generated with \`./run generate\`"
  exit 1
fi

# Define the msi output path
MSI_OUTPUT_PATH="releases/${SHORT_NAME}_${TODAY}_${BRANCH_NAME}.msi"
MSI_WIX_OBJ_OUTPUT_PATH="artifacts/installer/${SHORT_NAME}_${TODAY}_${BRANCH_NAME}.wixobj"

# Check if SFX config files exist
if [ ! -f "$WIX_SETUP_SCRIPT" ]; then
  echo "The SFX setup script does not exist, it can be generated with \`./run generate\`"
  exit 1
fi

# Delete any existing msi release file
if [ -f "$MSI_OUTPUT_PATH" ]; then
  rm "$MSI_OUTPUT_PATH" || exit 1
fi

# Start the package building process

echo "Building package for branch $BRANCH_NAME"
echo

# Define WiX command line extensions usage
WIX_COMMAND_LINE_EXT="-nologo -ext WixUIExtension -ext WixUtilExtension -ext WixBalExtension -ext WixNetFxExtension"

# Compile with WiX candle and link with WiX light
echo "Compiling $WIX_SETUP_SCRIPT with WiX candle..."
echo -ne
"$WIX_CANDLE_EXE_PATH" -arch x64 -out "$MSI_WIX_OBJ_OUTPUT_PATH" "$WIX_SETUP_SCRIPT" $WIX_COMMAND_LINE_EXT 1>/dev/null
if [ $? -ne 0 ]; then
  echo "WiX candle failed"
  exit 1
fi

echo "Linking $MSI_WIX_OBJ_OUTPUT_PATH with WiX light ..."
echo
WIX_PDB_OUTPUT_PATH="artifacts/installer/${SHORT_NAME}_${TODAY}_${BRANCH_NAME}.wixpdb"
WIX_PDB_COMMAND_LINE="-pdbout $WIX_PDB_OUTPUT_PATH"
"$WIX_LIGHT_EXE_PATH" -out "$MSI_OUTPUT_PATH" "$MSI_WIX_OBJ_OUTPUT_PATH" $WIX_COMMAND_LINE_EXT $WIX_PDB_COMMAND_LINE -sw1076 1>/dev/null

# Check if the zip file was created
if [ ! -f "$MSI_OUTPUT_PATH" ]; then
  echo "Failed to create MSI package"
  exit 1
fi

# Define ballet release path
#BALLET_RELEASE_DIR_PATH="../ballet/public/releases/win32/"
#BALLET_RELEASE_DIR_PATH=$(cygpath -w $(realpath $BALLET_RELEASE_DIR_PATH))

# Publish files to the ballet repo
publish_file "CHANGELOG.md" "../ballet/public/"
publish_file "$PROJECT_EXE_PATH" "releases/${SHORT_NAME}_${TODAY}_${BRANCH_NAME}_portable.exe"
#publish_file "$MSI_OUTPUT_PATH" "$BALLET_RELEASE_DIR_PATH/${PROJECT_PACKAGE_NAME}.msi"  

# Generate versions.json
# {
#    "name": "Wallet",
#    "description": "Wallet",
#    "versions": [
#        {
#            "version": "0.24.8",
#            "date": "2023-05-17T14:52:00.000Z",
#            "description": "Fix real-time refresh rate from 5 minutes to 1 minute.",
#            "package": {
#                "osx": {
#                    "url": "wallet_release_latest_backend.zip"
#                },
#                "windows": {
#                    "url": "wallet_release_latest_backend.msi"
#                }
#            }
#        }
#    ]
#}

# Define versions.json path
VERSIONS_JSON_PATH="releases/versions.json"

# Get last commit message
LAST_COMMIT_MESSAGE=$(git log -1 --pretty=%B)

# Create versions.json if it does not exist
echo "Creating versions.json file"
echo "{" > "$VERSIONS_JSON_PATH"
echo "  \"name\": \"$SHORT_NAME\"," >> "$VERSIONS_JSON_PATH"
echo "  \"description\": \"${SHORT_NAME}_${TODAY}_${BRANCH_NAME}\"," >> "$VERSIONS_JSON_PATH"
echo "  \"versions\": [" >> "$VERSIONS_JSON_PATH"
echo "    {" >> "$VERSIONS_JSON_PATH"
echo "      \"version\": \"$VERSION_MAJOR.$VERSION_MINOR.$VERSION_PATCH\"," >> "$VERSIONS_JSON_PATH"
echo "      \"date\": \"$(date -u +"%Y-%m-%dT%H:%M:%S.000Z")\"," >> "$VERSIONS_JSON_PATH"
echo "      \"description\": \"$LAST_COMMIT_MESSAGE\"," >> "$VERSIONS_JSON_PATH"
echo "      \"package\": {" >> "$VERSIONS_JSON_PATH"
echo "        \"windows\": {" >> "$VERSIONS_JSON_PATH"
echo "          \"url\": \"${SHORT_NAME}_${TODAY}_${BRANCH_NAME}.msi\"" >> "$VERSIONS_JSON_PATH"
echo "        }" >> "$VERSIONS_JSON_PATH"
echo "      }" >> "$VERSIONS_JSON_PATH"
echo "  ]" >> "$VERSIONS_JSON_PATH"
echo "}" >> "$VERSIONS_JSON_PATH"

publish_file "$VERSIONS_JSON_PATH" "../ballet/public/releases/versions.json"

# Print the build zip path
echo
echo "Built package: $(convert_path_to_file_link "$MSI_OUTPUT_PATH") ($VERSION_MAJOR.$VERSION_MINOR.$VERSION_PATCH)"
