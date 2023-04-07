#!/bin/bash
# Path: scripts\aidiff.sh

#
# Copyright 2023 Wiimag Inc. All rights reserved.
# License: https://equals-forty-two.com/LICENSE
#
# Use OpenAI to generate a short summary of every modified file in the current repo.
#

#
# Parse common command line arguments
#
POSITIONAL_ARGS=()
PAUSE=0
HELP=0
VERBOSE=0
NO_COLOR=0
COMMIT=0
CACHED=0
COMMAND_COUNTER=0

while [[ $# -gt 0 ]]; do
  #echo "\$1:\"$1\" \$2:\"$2\""
  case $1 in
    -h|--help)
      HELP=1
      shift # past argument
      ;;
    --verbose)
      VERBOSE=1
      shift # past argument
      ;;
    --no-color)
      NO_COLOR=1
      shift # past argument
      ;;
    --pause)
      PAUSE=1
      shift # past argument
      ;;
    --commit)
      COMMIT=1
      shift # past argument
      ;;
    --cached)
      CACHED=1
      shift # past argument
      ;;
    *)
      POSITIONAL_ARGS+=("$1") # save positional arg
      shift # past argument
      ;;
  esac
done


# Define a set of colors if NO_COLOR is not set
if [ $NO_COLOR -eq 0 ]; then
  bold=$(tput bold)
  normal=$(tput sgr0)
  green='\033[0;32m'
  dark_gray='\033[1;30m'
  light_gray='\033[0;37m'
  red='\033[0;31m'
  yellow='\033[1;33m'
  blue='\033[0;34m'
  purple='\033[0;35m'
  cyan='\033[0;36m'
  white='\033[1;37m'
  nc='\033[0m' # No Color
  italic=$(tput sitm)
  underline=$(tput smul)
  reset=$(tput sgr0)
else
  bold=""
  normal=""
  green=''
  dark_gray=''
  light_gray=''
  red=''
  yellow=''
  blue=''
  purple=''
  cyan=''
  white=''
  nc='' # No Color
  italic=''
  underline=''
  reset=''
fi

# Set the revision to use
COMMIT_REVISION=""
USE_RANGE_COMMIT=0

# Set the extra commit arguments
if [ $CACHED -eq 1 ]; then
  COMMIT_REVISION="HEAD~1"
  EXTRA_COMMIT_ARGS="--cached"
else
  EXTRA_COMMIT_ARGS=""
fi

# If POSITIONAL_ARGS is not empty then use the first argument as the commit
if [ ${#POSITIONAL_ARGS[@]} -gt 0 ]; then
    COMMIT_REVISION=${POSITIONAL_ARGS[0]}

    echo -e "${bold}Using commit:${normal} $COMMIT_REVISION"
fi

# Run git command to extract the list of modified files
if [ -z "$COMMIT_REVISION" ]; then
    USE_RANGE_COMMIT=0
    MODIFIED_FILES=$(git -c core.safecrlf=false diff --name-only ${POSITIONAL_ARGS[@]} $EXTRA_COMMIT_ARGS)
elif [ ${#POSITIONAL_ARGS[@]} -eq 1 ]; then
    
    # If we only have one revision then format with COMMIT_REVISION~1 COMMIT_REVISION
    USE_RANGE_COMMIT=0
    MODIFIED_FILES=$(git -c core.safecrlf=false diff --name-only ${COMMIT_REVISION}~1 $COMMIT_REVISION $EXTRA_COMMIT_ARGS)
else
    USE_RANGE_COMMIT=1
    MODIFIED_FILES=$(git -c core.safecrlf=false diff --name-only ${POSITIONAL_ARGS[@]} $EXTRA_COMMIT_ARGS])
fi

# Split the list of modified files into an array
IFS=$'

'
MODIFIED_FILES_ARRAY=($MODIFIED_FILES)

# Create the artifacts directory if it doesn't exist
AIDIFF_PATH="artifacts/aidiff"
if [ ! -d $AIDIFF_PATH ]; then
    mkdir -p $AIDIFF_PATH
fi

# Create all summary file
ALL_SUMMARIES_FILE_PATH="$AIDIFF_PATH/all_summaries.txt"
echo "" > $ALL_SUMMARIES_FILE_PATH

# Define curl headers
HEADER_JSON="Content-Type: application/json"
HEADER_AUTHORIZATION="Authorization: Bearer $OPENAI_API_KEY"

# For each file get the diff and pass it to OpenAI
#echo "Modified files:"
for MODIFIED_FILE in "${MODIFIED_FILES_ARRAY[@]}"
do
    # Get the file diff, make sure git do not output any warnings
    if [ $USE_RANGE_COMMIT -eq 1 ]; then
        DIFF=$(git -c core.safecrlf=false diff ${POSITIONAL_ARGS[@]} -- $MODIFIED_FILE 2>&1)
    else
        DIFF=$(git -c core.safecrlf=false diff ${COMMIT_REVISION}~1 $COMMIT_REVISION -- $MODIFIED_FILE 2>&1)
    fi

    # If using --verbose print the diff command
    if [ $VERBOSE -eq 1 ]; then
        echo "> git -c core.safecrlf=false show ${POSITIONAL_ARGS[@]} -- $MODIFIED_FILE"
    fi

    # Check if last git command failed
    if [ $? -ne 0 ]; then
        echo "Failed to get diff for file $MODIFIED_FILE"
        continue
    fi

    # Continue if the diff is empty or if a an error occurred
    if [ -z "$DIFF" ]; then
        continue
    fi

    # Truncate the DIFF to ~3000 characters
    DIFF=$(echo $DIFF | cut -c -3500)

    echo -ne "${bold}$MODIFIED_FILE:${normal} "

    # Generate a prompt for OpenAI
    PROMPT="These are changes to $MODIFIED_FILE. Please summarize the changes in this file in a single sentence. The summary should be short and to the point.".

    # Add the diff to the prompt on a new line
    PROMPT="$PROMPT

\`\`\`
$DIFF
\`\`\` ---"

    # Replace \ with \\
    PROMPT=$(echo $PROMPT | sed 's/\\/\\\\/g' | sed 's/"/\\"/g')

    # Replace all \t with \\t
    PROMPT=$(echo $PROMPT | sed 's/\t/\\t/g')

    # Replace all \" with \\\"
    PROMPT=$(echo $PROMPT | sed 's/\n/\\n/g')

    #echo "$PROMPT"

    # Get file base name
    FILE_BASE_NAME=$(basename $MODIFIED_FILE)

    # Build output file path
    JSON_PROMPT_FILE_PATH="$AIDIFF_PATH/$FILE_BASE_NAME.json"

    # Generate a JSON file under artifacts for the given file to send the prompt to OpenAI
    echo "{" > $JSON_PROMPT_FILE_PATH
    echo "  \"model\": \"text-davinci-003\"," >> $JSON_PROMPT_FILE_PATH
    echo "  \"prompt\": \"$PROMPT\"," >> $JSON_PROMPT_FILE_PATH
    echo "  \"temperature\": 0.2," >> $JSON_PROMPT_FILE_PATH
    echo "  \"max_tokens\": 200," >> $JSON_PROMPT_FILE_PATH
    echo "  \"top_p\": 0.8," >> $JSON_PROMPT_FILE_PATH
    echo "  \"frequency_penalty\": 0.9," >> $JSON_PROMPT_FILE_PATH
    echo "  \"presence_penalty\": 0.6," >> $JSON_PROMPT_FILE_PATH
    echo "  \"stop\": [\"---\"]" >> $JSON_PROMPT_FILE_PATH
    echo "}" >> $JSON_PROMPT_FILE_PATH

    # Run curl to send the prompt to OpenAI
    SUMMARY_JSON=$(curl -s -X POST https://api.openai.com/v1/completions -H "$HEADER_JSON" -H "$HEADER_AUTHORIZATION" -d @$JSON_PROMPT_FILE_PATH)

    # Extract the summary from the JSON response from the "text": "..." field, make sure to preserve espaced characters
    SUMMARY=$(echo $SUMMARY_JSON | sed 's/.*"text":"//' | sed 's/"[,\}].*//')
    SUMMARY=$(echo $SUMMARY | sed 's/^[ \t]*//;s/[ \t]*$//')
    SUMMARY=$(echo $SUMMARY | sed 's/\\n//g' | sed 's/\\t//g' | sed 's/\\r//g' | sed 's/\\//g')

    echo "$SUMMARY"

    # Debug output if --verbose flag was used
    if [ $VERBOSE -eq 1 ]; then
        echo "> curl -s -X POST https://api.openai.com/v1/completions -H \"$HEADER_JSON\" -H \"Authorization: Bearer \$OPENAI_API_KEY\" -d @$JSON_PROMPT_FILE_PATH"
        echo "< $SUMMARY_JSON"
        echo 
    fi

    # Append the summary to the all summaries file
    echo "* $MODIFIED_FILE: $SUMMARY" >> $ALL_SUMMARIES_FILE_PATH

    # Pause
    if [ $PAUSE -eq 1 ]; then
        read -p "Press enter to continue"
    fi
done

# Finally ask OpenAI to summarize all the summaries into a single summary if applicable
if [ ${#MODIFIED_FILES_ARRAY[@]} -gt 1 ]; then

    # Generate a prompt for OpenAI
    PROMPT="These are changes to multiple files. Please consolidate the changes in these files in a single sentence and markdown format for symbol decoration. The summary should be short and to the point.".

    # Add the summary to the prompt on a new line
    PROMPT="$PROMPT 
$(cat $ALL_SUMMARIES_FILE_PATH)
---"

    # Cleanup the prompt to make sure it is valid JSON
    PROMPT=$(echo $PROMPT | sed 's/\\/\\\\/g' | sed 's/"/\\"/g')
    PROMPT=$(echo $PROMPT | sed 's/\t/\\t/g')
    PROMPT=$(echo $PROMPT | sed 's/\n/\\n/g')

    # If COMMIT_REVISION is empty, use the current branch name
    if [ -z "$COMMIT_REVISION" ]; then
        COMMIT_REVISION=$(git rev-parse --abbrev-ref HEAD)
    fi

    # Generate a valid filename of COMMIT and adding .json as an extension
    COMMIT_BASE_NAME=$(echo $COMMIT_REVISION | sed 's/[^a-zA-Z0-9]/_/g')
    COMMIT_FILE_NAME=${COMMIT_BASE_NAME}.json

    # Build output file path
    JSON_PROMPT_FILE_PATH="$AIDIFF_PATH/$COMMIT_FILE_NAME"

    # Generate a JSON file under artifacts for the given file to send the prompt to OpenAI
    echo "{" > $JSON_PROMPT_FILE_PATH
    echo "  \"model\": \"text-davinci-003\"," >> $JSON_PROMPT_FILE_PATH
    echo "  \"prompt\": \"$PROMPT\"," >> $JSON_PROMPT_FILE_PATH
    echo "  \"temperature\": 0.1," >> $JSON_PROMPT_FILE_PATH
    echo "  \"max_tokens\": 100," >> $JSON_PROMPT_FILE_PATH
    echo "  \"top_p\": 1," >> $JSON_PROMPT_FILE_PATH
    echo "  \"frequency_penalty\": 0.2," >> $JSON_PROMPT_FILE_PATH
    echo "  \"presence_penalty\": 0.2," >> $JSON_PROMPT_FILE_PATH
    echo "  \"stop\": [\"---\"]" >> $JSON_PROMPT_FILE_PATH
    echo "}" >> $JSON_PROMPT_FILE_PATH

    # Run curl to send the prompt to OpenAI
    SUMMARY_JSON=$(curl -s -X POST https://api.openai.com/v1/completions -H "$HEADER_JSON" -H "$HEADER_AUTHORIZATION" -d @$JSON_PROMPT_FILE_PATH)

    # Parse the summary from the JSON response from the "text": "..." field, make sure to preserve espaced characters
    SUMMARY=$(echo $SUMMARY_JSON | sed 's/.*"text":"//' | sed 's/"[,\}].*//')
    SUMMARY=$(echo $SUMMARY | sed 's/^[ \t]*//;s/[ \t]*$//')
    SUMMARY=$(echo $SUMMARY | sed 's/\\n//g' | sed 's/\\t//g' | sed 's/\\r//g' | sed 's/\\//g')

    echo 
    echo "${bold}Summary:${normal} $SUMMARY"
    echo 

fi

ALL_FILE_CHANGES_SUMMARY=$(cat $ALL_SUMMARIES_FILE_PATH)
COMMIT_MESSAGE_FILE_PATH="$AIDIFF_PATH/${COMMIT_BASE_NAME}.commit.txt"

# Write commit message to file first
echo "$SUMMARY" > $COMMIT_MESSAGE_FILE_PATH

# Append the all file changes summary to the commit message file only if there are more than 1 file changes
if [ ${#MODIFIED_FILES_ARRAY[@]} -gt 1 ]; then
    echo "$ALL_FILE_CHANGES_SUMMARY" >> $COMMIT_MESSAGE_FILE_PATH
    echo "" >> $COMMIT_MESSAGE_FILE_PATH
    echo "Generated by $(basename $0) on $(date)" >> $COMMIT_MESSAGE_FILE_PATH
fi

# If --commit flag was used, commit the changes and amend the commit message with the summary
if [ $COMMIT -eq 1 ]; then

    # You can run the following command to commit the changes
    echo "git commit -a -F $COMMIT_MESSAGE_FILE_PATH"
    
else

    # If --commit flag was not used, open the commit message file in the default editor
    start ${COMMIT_MESSAGE_FILE_PATH}

fi
