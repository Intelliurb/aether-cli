#!/bin/sh
DIR=$(dirname "$0")
. "$DIR/utils.sh"

if command -v ip >/dev/null 2>&1; then
    ip route show
elif command -v netstat >/dev/null 2>&1; then
    netstat -rn
else
    get_msg "script_net_route_missing" "No routing table command found (ip/netstat)."
    exit 1
fi
