#!/bin/bash

if [ $# -ne 0 ] && [ $1 == "tests" ]; then
    shift
fi

if [ $# -ne 0 ] && [ $1 == "start" ]; then
    shift
fi

xcodebuild -workspace projects/xcode/wallet.xcworkspace -scheme "Debug" build SYMROOT="../../build" $@
