#!/bin/bash
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$DIR/scripts/utils.sh"
check_write_permission "$AETHERCLI_CONFIG"

if [ "$#" -ne 1 ]; then
    get_msg "script_set_usage" "Usage: set statusbar <on|off>"
    exit 1
fi

if [ "$1" = "on" ]; then
    VAL="true"
elif [ "$1" = "off" ]; then
    VAL="false"
else
    get_msg "script_set_err_arg" "Error: argument must be 'on' or 'off'."
    exit 1
fi

USER_CONFIG_DIR="$HOME/.aethercli"
mkdir -p "$USER_CONFIG_DIR"
echo "$VAL" > "$USER_CONFIG_DIR/statusbar"

msg=$(get_msg "script_set_success" "Status bar set to _VAL_. Changes will take effect in the next session.")
echo "${msg/_VAL_/$1}"
