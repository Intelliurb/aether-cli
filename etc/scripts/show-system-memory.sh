#!/bin/sh
DIR=$(dirname "$0")
. "$DIR/utils.sh"

if command -v free >/dev/null 2>&1; then
    free -h
elif command -v vm_stat >/dev/null 2>&1; then
    vm_stat
elif command -v vmstat >/dev/null 2>&1; then
    vmstat
else
    get_msg "script_memory_missing" "No memory usage command found (free/vm_stat/vmstat)."
    exit 1
fi
