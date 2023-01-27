#!/bin/bash

if [ $# -ne 0 ] && [ $1 == "tests" ]; then
    shift
fi

xcodebuild -workspace projects/xcode/infineis.xcworkspace -scheme "infineis Debug" build SYMROOT="../../build" $@
