#!/bin/sh
if command -v ip >/dev/null 2>&1; then
    ip addr show
elif command -v ifconfig >/dev/null 2>&1; then
    ifconfig
else
    echo "No network interface command found (ip/ifconfig)."
    exit 1
fi
