#!/bin/sh
DIR=$(dirname "$0")
. "$DIR/utils.sh"

if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
    get_msg "script_find_usage" "Usage: find files <pattern> [directory]"
    exit 1
fi

PATTERN=$1
SEARCH_DIR=${2:-.}
if [ ! -d "$SEARCH_DIR" ]; then
    get_msg "script_find_directory_missing" "Error: directory not found."
    exit 1
fi

find "$SEARCH_DIR" -type f -name "$PATTERN"
