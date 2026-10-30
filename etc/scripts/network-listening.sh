#!/bin/sh
DIR=$(dirname "$0")
. "$DIR/utils.sh"

if command -v ss >/dev/null 2>&1; then
    ss -lntu
elif command -v lsof >/dev/null 2>&1; then
    lsof -nP -iTCP -sTCP:LISTEN
    lsof -nP -iUDP
elif command -v netstat >/dev/null 2>&1; then
    netstat -an | awk 'tolower($1) ~ /^tcp|^udp/ && (toupper($NF) == "LISTEN" || tolower($1) ~ /^udp/)'
else
    get_msg "script_listening_missing" "No listening ports command found (ss/lsof/netstat)."
    exit 1
fi
