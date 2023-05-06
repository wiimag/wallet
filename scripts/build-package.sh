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
  BRANCH_NAME="release"
fi

# If branch name is main, rename to setup
if [ "$BRANCH_NAME" = "main" ]; then
  BRANCH_NAME="release"
fi

# Load the WinRar path from Program Files
WIX_BIN_DIR="C:\Tools\wix311-binaries"
WIX_CANDLE_EXE_PATH="$WIX_BIN_DIR\candle.exe"
WIX_LIGHT_EXE_PATH="$WIX_BIN_DIR\light.exe"

# Make sure WiX candle and light are available
if [ ! -f "$WIX_CANDLE_EXE_PATH" ]; then
  echo "The WiX candle.exe file does not exist in the Program Files folder"
  exit 1
fi

if [ ! -f "$WIX_LIGHT_EXE_PATH" ]; then
  echo "The WiX light.exe file does not exist in the Program Files folder"
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

  BUILD_OPTIONS=()
  BUILD_OPTIONS+=("-DBUILD_ENABLE_TESTS=OFF")

  if [[ "$*" == *backend* ]]; then
    BUILD_OPTIONS+=("-DBUILD_ENABLE_BACKEND=ON")
    BUILD_OPTIONS+=("-DBUILD_ENABLE_DEVELOPMENT=OFF")

    # Add _backend suffix to the branch name
    BRANCH_NAME="${BRANCH_NAME}_backend"
  else
    BUILD_OPTIONS+=("-DBUILD_ENABLE_BACKEND=OFF")
  fi

  # Print build options
  echo "Build options:"
  for option in "${BUILD_OPTIONS[@]}"; do
    echo "  $option"
  done

  # Check if deploy is in the command line
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
  exit 1
fi

# Create the artifacts folder if it does not exist
if [ ! -d "releases" ]; then
  mkdir releases
fi

# Define today's date as YYYY_MM_DD
TODAY=$(date +"%Y_%m_%d")

# Create a copy of the exe named *_app.exe to provide a standalone exe
cp "$PROJECT_EXE_PATH" "releases/${SHORT_NAME}_${TODAY}_${BRANCH_NAME}_portable.exe"

# Define the msi output path
MSI_OUTPUT_PATH="releases/${SHORT_NAME}_${TODAY}_${BRANCH_NAME}.msi"
MSI_WIX_OBJ_OUTPUT_PATH="artifacts/installer/${SHORT_NAME}_${TODAY}_${BRANCH_NAME}.wixobj"

# Define sfx banner low and high res
SFX_BANNER_LOW_RES="resources/banner_low_93_302.png"
SFX_BANNER_HIGH_RES="resources/banner_high_186_604.png"

# Define package icon
PACKAGE_ICON="resources/main.ico"

# Define SFX setup script
WIX_SETUP_SCRIPT="resources/setup.wxs"

# Check if SFX config files exist
if [ ! -f "$WIX_SETUP_SCRIPT" ]; then
  echo "The SFX setup script does not exist, it can be generated with \`./run generate\`"
  exit 1
fi

# Delete any existing msi release file
if [ -f "$MSI_OUTPUT_PATH" ]; then
  rm "$MSI_OUTPUT_PATH" || exit 1
fi

# Define WiX command line extensions usage
WIX_COMMAND_LINE_EXT="-ext WixUIExtension -ext WixUtilExtension -ext WixBalExtension -ext WixNetFxExtension"

# Compile with WiX candle and link with WiX light
echo
echo "Compiling with WiX candle"
echo
"$WIX_CANDLE_EXE_PATH" -arch x64 -out "$MSI_WIX_OBJ_OUTPUT_PATH" "$WIX_SETUP_SCRIPT" $WIX_COMMAND_LINE_EXT
if [ $? -ne 0 ]; then
  echo "WiX candle failed"
  exit 1
fi

# Define path to output wixpdb file
WIX_PDB_OUTPUT_PATH="artifacts/installer/${SHORT_NAME}_${TODAY}_${BRANCH_NAME}.wixpdb"

# Define command line to output wixpdb file
WIX_PDB_COMMAND_LINE="-pdbout $WIX_PDB_OUTPUT_PATH"

echo
echo "Linking with WiX light"
echo
"$WIX_LIGHT_EXE_PATH" -out "$MSI_OUTPUT_PATH" "$MSI_WIX_OBJ_OUTPUT_PATH" $WIX_COMMAND_LINE_EXT $WIX_PDB_COMMAND_LINE -sw1076
echo

# Check if the zip file was created
if [ ! -f "$MSI_OUTPUT_PATH" ]; then
  echo "The MSI file was not created"
  exit 1
fi

# Make the zip output path absolute and convert to windows path using cygwin
MSI_OUTPUT_PATH=$(cygpath -w $(realpath $MSI_OUTPUT_PATH))

# Convert the zip output path to a file:// url
MSI_OUTPUT_PATH=${MSI_OUTPUT_PATH//\\/\/}

# Print the build zip path
echo "Build package: file://$MSI_OUTPUT_PATH"

# Run Windows Defender to scan the zip file
# https://docs.microsoft.com/en-us/windows/security/threat-protection/windows-defender-antivirus/command-line-arguments-windows-defender-antivirus

# Define the Windows Defender path
WINDOWS_DEFENDER_PATH="C:\Program Files\Windows Defender\MpCmdRun.exe"

# Define ballet release path
BALLET_RELEASE_DIR_PATH="../ballet/public/releases/win32/"
BALLET_RELEASE_DIR_PATH=$(cygpath -w $(realpath $BALLET_RELEASE_DIR_PATH))
echo "Ballet release dir path: file://${MSI_OUTPUT_PATH//\\/\/}"

# Make sure the Windows Defender path exists
if [ -f "$WINDOWS_DEFENDER_PATH" ]; then
  # Run Windows Defender to scan the zip file
  "$WINDOWS_DEFENDER_PATH" -Scan -ScanType 3 -File "$MSI_OUTPUT_PATH"
  if [ $? -ne 0 ]; then
    echo "Windows Defender scan failed"
    #exit 1
  fi

  # If command line has --run, run the produced exe
  if [[ "$*" == *-run* ]]; then
    echo "Running $MSI_OUTPUT_PATH"
    start "$MSI_OUTPUT_PATH"
  fi

  # Copy to ballet repo
  if [[ "$*" == *backend* ]]; then

    # Copy changelog to ballet repo
    cp "CHANGELOG.md" "../ballet/public/"
    echo "Copied to ballet repo: ../ballet/public/CHANGELOG.md"

    BALLET_RELEASE_PATH="$BALLET_RELEASE_DIR_PATH/wallet_release_latest_backend.msi"
    cp "$MSI_OUTPUT_PATH" "$BALLET_RELEASE_PATH"
    echo "Copied to ballet repo: $BALLET_RELEASE_PATH"

    # Copy portable exe to ballet repo
    cp "$PROJECT_EXE_PATH" "$BALLET_RELEASE_DIR_PATH/wallet_release_latest_backend_portable.exe"
    echo "Copied to ballet repo: $BALLET_RELEASE_DIR_PATH/wallet_release_latest_backend_portable.exe"
  fi

else
  echo "Cannot scan the MSI file with Windows Defender, the MpCmdRun.exe file does not exist in the Program Files folder"
fi
