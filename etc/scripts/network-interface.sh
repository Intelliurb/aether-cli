#!/bin/sh
DIR=$(dirname "$0")
. "$DIR/utils.sh"

if command -v ip >/dev/null 2>&1; then
    printf '%s\n' "$(get_msg "script_interface_addresses" "Interface addresses:")"
    ip -o -4 addr show | awk '{ print $2 ": " $4 }'
elif command -v ifconfig >/dev/null 2>&1; then
    printf '%s\n' "$(get_msg "script_interface_addresses" "Interface addresses:")"
    ifconfig | awk '/^[[:alnum:]][^:]*:/ { iface=$1; sub(/:$/, "", iface) } /inet / { print iface ": " $2 " " $3 " " $4 }'
else
    get_msg "script_net_interface_missing" "No network interface command found (ip/ifconfig)."
    exit 1
fi
