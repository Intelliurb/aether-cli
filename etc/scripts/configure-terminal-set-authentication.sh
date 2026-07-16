#!/bin/bash
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LANG_FILE="$DIR/lang_${AETHERCLI_LANG:-en}.json"

get_msg() {
    local key="$1"
    local default_msg="$2"
    if [ -f "$LANG_FILE" ]; then
        local val=$(jq -r ".$key // \"$default_msg\"" "$LANG_FILE" 2>/dev/null)
        if [ "$val" != "null" ]; then echo "$val"; return; fi
    fi
    echo "$default_msg"
}

if [ "$#" -ne 1 ]; then
    get_msg "script_set_auth_usage" "Usage: set authentication <on|off>"
    exit 1
fi

if [ "$1" = "on" ]; then
    VAL="true"
elif [ "$1" = "off" ]; then
    VAL="false"
else
    get_msg "script_set_auth_err_arg" "Error: argument must be 'on' or 'off'."
    exit 1
fi

USER_CONFIG_DIR="$HOME/.aethercli"
mkdir -p "$USER_CONFIG_DIR"
echo "$VAL" > "$USER_CONFIG_DIR/authentication"

msg=$(get_msg "script_set_auth_success" "Authentication set to _VAL_. Changes will take effect in the next session.")
echo "${msg/_VAL_/$1}"
