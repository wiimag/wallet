#!/bin/bash
# Create the MacOS icon set
# Source: https://www.codingforentrepreneurs.com/blog/create-icns-icons-for-macos-apps/

cd ./resources
input_filepath=logo.png
output_iconset_name="App.iconset"
mkdir $output_iconset_name

sips -z 16 16     $input_filepath --out "${output_iconset_name}/icon_16x16.png"
sips -z 32 32     $input_filepath --out "${output_iconset_name}/icon_16x16@2x.png"

sips -z 18 18     $input_filepath --out "${output_iconset_name}/icon_18x18.png"
sips -z 36 36     $input_filepath --out "${output_iconset_name}/icon_18x18@2x.png"

sips -z 24 24     $input_filepath --out "${output_iconset_name}/icon_24x24.png"
sips -z 48 48     $input_filepath --out "${output_iconset_name}/icon_24x24@2x.png"

sips -z 32 32     $input_filepath --out "${output_iconset_name}/icon_32x32.png"
sips -z 64 64     $input_filepath --out "${output_iconset_name}/icon_32x32@2x.png"

sips -z 64 64     $input_filepath --out "${output_iconset_name}/icon_64x64.png"
sips -z 128 128   $input_filepath --out "${output_iconset_name}/icon_64x64@2x.png"

sips -z 128 128   $input_filepath --out "${output_iconset_name}/icon_128x128.png"
sips -z 256 256   $input_filepath --out "${output_iconset_name}/icon_128x128@2x.png"

sips -z 256 256   $input_filepath --out "${output_iconset_name}/icon_256x256.png"
sips -z 512 512   $input_filepath --out "${output_iconset_name}/icon_256x256@2x.png"

sips -z 512 512   $input_filepath --out "${output_iconset_name}/icon_512x512.png"
sips -z 1024 1024 $input_filepath --out "${output_iconset_name}/icon_512x512@2x.png"

sips -z 1024 1024 $input_filepath --out "${output_iconset_name}/icon_1024x1024.png"

iconutil -c icns $output_iconset_name
#rm -R $output_iconset_name
