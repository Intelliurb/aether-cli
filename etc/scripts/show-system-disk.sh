#!/bin/sh
DIR=$(dirname "$0")
. "$DIR/utils.sh"

if command -v df >/dev/null 2>&1; then
    df -h
else
    get_msg "script_disk_missing" "No disk usage command found (df)."
    exit 1
fi
