#!/bin/sh
DIR=$(dirname "$0")
. "$DIR/utils.sh"

if command -v ip >/dev/null 2>&1; then
    ip -s link show
elif command -v netstat >/dev/null 2>&1; then
    netstat -ib
elif command -v ifconfig >/dev/null 2>&1; then
    ifconfig -a
else
    get_msg "script_interface_stats_missing" "No interface statistics command found (ip/netstat/ifconfig)."
    exit 1
fi
