#!/bin/bash

#
# Copyright 2022-2023 Wiimag Inc. All rights reserved.
# License: https://equals-forty-two.com/LICENSE
#
# Run script to execute most common operations used when working on the application framework.
#

APP_NAME=Wallet
SHORT_NAME=wallet

bold=$(tput bold)
normal=$(tput sgr0)
progname=$(basename $0)
unameOut="$(uname -s)"

# Determine running platform
case "${unameOut}" in
    Linux*)     machine=Linux;;
    Darwin*)    machine=Mac;;
    CYGWIN*)    machine=Cygwin;;
    MINGW*)     machine=MinGw;;
    *)          machine="UNKNOWN:${unameOut}"
esac
#echo ${machine}

is_number() { 
  [ "$1" ] && [ -z "${1//[0-9]}" ] ;
}

#
# Parse common command line arguments
#

POSITIONAL_ARGS=()
RUN=1 # Indicate if the default build should be run at the end
BUILD=0
BUILD_DEBUG=0
BUILD_RELEASE=1
BUILD_DEPLOY=0
TESTS=0
OPEN=0
OPEN_WORKSPACE=0
START=0
PRINT=0
PRINT_LARGEFILES=0
HELP=0
VERBOSE=0
DIFF=0

while [[ $# -gt 0 ]]; do
  #echo "\$1:\"$1\" \$2:\"$2\""
  case $1 in
    -h|--help)
      HELP=1
      shift # past argument
      ;;
    --verbose)
      VERBOSE=1
      POSITIONAL_ARGS+=("$1") # save positional arg
      shift # past argument
      ;;
    -n|--name)
      SHORT_NAME="$2"
      shift # past argument
      shift # past value
      ;;
    -a|--app-name)
      APP_NAME="$2"
      shift # past argument
      shift # past value
      ;;
    b|build)
      BUILD=1
      shift # past argument

      # The release argument is the default, but remove it if it was specified
      if [ $# -ne 0 ] && [ $1 = "release" ]; then
        BUILD_RELEASE=1
        shift
      fi

      if [ $# -ne 0 ] && [ $1 = "debug" ]; then
        BUILD_DEBUG=1
        shift
      fi

      if [ $# -ne 0 ] && [ $1 = "deploy" ]; then
        BUILD_DEPLOY=1
        shift
      fi
      ;;
    p|print)
      PRINT=1
      shift # past argument

      # The release argument is the default, but remove it if it was specified
      if [ $# -ne 0 ] && [ $1 = "largefiles" ]; then
        PRINT_LARGEFILES=1
        shift
      fi
      ;;
    o|open)
      OPEN=1
      shift # past argument

      # The release argument is the default, but remove it if it was specified
      if [ $# -ne 0 ] && ([ $1 == "workspace" ] || [ $1 == "w" ]); then
        OPEN_WORKSPACE=1
        shift
      fi
      ;;
    t|tests)
      TESTS=1
      shift # past argument
      ;;
    d|diff)
      DIFF=1
      shift # past argument
      ;;
    s|start)
      RUN=1
      START=1
      shift # past argument
      ;;
    *)
      POSITIONAL_ARGS+=("$1") # save positional arg
      shift # past argument
      ;;
  esac
done

#
# Check if help was requested
#
if [ $HELP -eq 1 ]; then

  echo "  Usage: $progname [${bold}b${normal}uild [debug|release|deploy]] "
  echo "             [${bold}t${normal}ests] " 
  echo "             [${bold}o${normal}pen [${bold}w${normal}orkspace]] "
  echo "             [${bold}p${normal}rint [largefiles] "
  echo "             [${bold}d${normal}iff [main|release/1.0.0|...] "
  echo "             [${bold}s${normal}tart] [--option=<value>] ...additional arguments passed to last command"
  echo ""
  echo "  Commands:"
  echo "    ${bold}b${normal}uild [debug|release]  Build the solution in debug or release (release if used by default)"
  echo "    ${bold}t${normal}ests                  Run application tests (i.e. using --run-tests)"
  echo "    ${bold}o${normal}pen [workspace]       Open the application solution workspace (i.e. Visual Studio or Xcode IDE)"
  echo "    ${bold}p${normal}rint [largefiles|...] Print various solution information or stats"
  echo "    ${bold}d${normal}iff [...]             Diff the current branch with the specified remote branch (i.e. main, release/1.0.0, etc.)"
  echo "    ${bold}s${normal}tart                  Launch the application after running other commands "
  echo ""
  echo "    If no commands are specified, we start the application by default, i.e. './run')"
  echo ""
  echo "    Commands can be chained one after the other, i.e. './run build tests --success=true --verbose'"
  echo ""
  echo "  Optional arguments:"
  echo "   -h, --help            Show this help message and exit"
  echo "   -n, --name <name>     Specify the solution name (used to open the workspace are launch the exe on Windows)"
  echo "   -a, --app-name <name> Specify the App name to launch application on MacOS"
  echo "   --verbose             Increase the verbosity of the bash script"
  echo ""
  echo "  Examples:"
  echo "    ./run build                         Build the solution"
  echo "    ./run build tests start             Build then run tests and finally lanch the application"
  echo "    ./run build tests --success=true    Build then run tests passing the --success=true argument to test framework"
  echo "    ./run print largefiles 50           Print top 50 largest files in the repo."
  echo "    ./run diff                          Diff the current branch with the main branch (default)"
  echo "    ./run diff release/1.0              Diff the current branch with the remote release 1.0 branch"
  echo ""
  exit 0

fi

if [ $VERBOSE -eq 1 ]; then

  # Print all set variables
  echo "APP_NAME: $APP_NAME"
  echo "SHORT_NAME: $SHORT_NAME"
  echo "BUILD: $BUILD"
  echo "  BUILD_DEBUG: $BUILD_DEBUG"
  echo "  BUILD_RELEASE: $BUILD_RELEASE"
  echo "  BUILD_DEPLOY: $BUILD_DEPLOY"
  echo "OPEN: $OPEN"
  echo "  OPEN_WORKSPACE: $OPEN_WORKSPACE"
  echo "PRINT: $PRINT"
  echo "  PRINT_LARGEFILES: $PRINT_LARGEFILES"
  echo "TESTS: $TESTS"
  echo "DIFF: $DIFF"
  echo "START: $START"
  echo "HELP: $HELP"
  echo "VERBOSE: $VERBOSE"

  # Echo positional args
  echo "Positional arguments: ${POSITIONAL_ARGS[@]}"
fi

#
# Build solution
#
if [ $BUILD -eq 1 ]; then

  if [ $machine == "MinGw" ]; then
    if [ $BUILD_DEBUG -eq 1 ]; then
      ./scripts/build_win_debug.bat $POSITIONAL_ARGS
    elif  [ $BUILD_DEPLOY -eq 1 ]; then
      ./scripts/build_win_deploy.bat $POSITIONAL_ARGS
    else
      ./scripts/build_win_release.bat $POSITIONAL_ARGS
    fi
  elif [ ${machine} == "Mac" ]; then
    if [ $BUILD_DEBUG -eq 1 ]; then
      ./scripts/build_osx_debug.sh $POSITIONAL_ARGS
    else
      ./scripts/build_osx_release.sh $POSITIONAL_ARGS
    fi
  fi

  retval=$?
  if [ $retval -ne 0 ]; then
      echo
      echo "===BUILD FAILED ($retval)==="
      echo
      exit $retval
  fi

  RUN=0
fi

#
# GIT DIFF
#
if [ $DIFF -eq 1 ]; then

    # Check if we have a remote branch to diff against and make sure the argument doesn't start with -- or -
    if [ ${#POSITIONAL_ARGS[@]} -eq 0 ] || [[ ${POSITIONAL_ARGS[0]} == --* ]] || [[ ${POSITIONAL_ARGS[0]} == -* ]]; then
        # Insert main at position 0 and push other arguments
        POSITIONAL_ARGS=("${POSITIONAL_ARGS[@]:0:0}" "main" "${POSITIONAL_ARGS[@]:0}")
    fi
    
    # Diff the current branch with the specified remote branch
    git difftool -d "origin/${POSITIONAL_ARGS[0]}" ${POSITIONAL_ARGS[@]:1}

    # Check return value
    retval=$?
    if [ $retval -ne 0 ]; then
        echo
        echo "===DIFF FAILED ($retval)==="
        echo
        exit $retval
    fi

    RUN=0
fi

#
# Open IDE
#
if [ $OPEN -eq 1 ]; then

    if [ $OPEN_WORKSPACE -eq 1 ]; then

        if [ $machine == "MinGw" ]; then
            start projects/vs2022/${SHORT_NAME}.sln
        elif [ ${machine} == "Mac" ]; then
            open projects/xcode/${SHORT_NAME}.xcworkspace
        fi

        retval=$?
        if [ $retval -ne 0 ]; then
            echo
            echo "===OPEN WORKSPACE FAILED ($retval)==="
            echo
            exit $retval
        fi

        RUN=0
    fi
fi

#
# Print various solution information
#
if [ $PRINT -eq 1 ]; then

    if [ $PRINT_LARGEFILES -eq 1 ]; then

        TOP_COUNT=20
        if is_number ${POSITIONAL_ARGS[0]}; then
          TOP_COUNT=${POSITIONAL_ARGS[0]}
        fi

        while read -r largefile; do
          echo $largefile | awk '{printf "%s %s ", $1, $3 ; system("git rev-list --all --objects | grep " $1 " | cut -d \" \" -f 2-")}'
        done <<< "$(git rev-list --all --objects | awk '{print $1}' | git cat-file --batch-check | sort -k3nr | head -n $TOP_COUNT)"

        retval=$?
        if [ $retval -ne 0 ]; then
            echo
            echo "=== PRINT FAILED ($retval)==="
            echo
            exit $retval
        fi

        RUN=0
    fi
fi

#
# Run tests
#
if [ $TESTS -eq 1 ]; then

    if [ ${machine} == "MinGw" ]; then
        ./build/${SHORT_NAME}.exe --run-tests $POSITIONAL_ARGS
    elif [ ${machine} == "Mac" ]; then
        ./build/${APP_NAME}.app/Contents/MacOS/${APP_NAME} --run-tests $POSITIONAL_ARGS
    fi

    retval=$?
    if [ $retval -ne 0 ]; then
        echo
        echo "===TESTS FAILED ($retval)==="
        echo
        cat artifacts/tests.log
        exit $retval
    fi

    echo
    echo "===TESTS SUCCESSED==="
    echo

    # Print test results
    cat artifacts/tests.log

    RUN=0
fi

# Check if we have an explicit command to run/start the application
if [ $START -eq 1 ]; then
    RUN=1
fi

# Finally run the application if requested.
if [ $RUN -eq 1 ]; then

    if [ ${machine} == "MinGw" ]; then
        ./build/${SHORT_NAME}.exe ${POSITIONAL_ARGS[@]}
    elif [ ${machine} == "Mac" ]; then
        ./build/${APP_NAME}.app/Contents/MacOS/${APP_NAME} ${POSITIONAL_ARGS[@]}
    fi

    retval=$?
    if [ $retval -ne 0 ]; then
        echo
        echo "===RUN FAILED ($retval)==="
        echo
        exit $retval
    fi
fi

exit $?