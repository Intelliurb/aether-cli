#!/bin/bash
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$DIR/scripts/utils.sh"
check_write_permission "$AETHERCLI_CONFIG"

if [ "$#" -ne 1 ]; then
    get_msg "script_set_auth_usage" "Usage: set authentication <on|off>"
    exit 1
fi

if [ "$AETHERCLI_USER" != "admin" ]; then
    get_msg "script_admin_only" "% Error: Only the 'admin' user can perform this action."
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

TMP_FILE=$(mktemp "$(dirname "$AETHERCLI_CONFIG")/.aethercli-config.XXXXXX") || exit 1
if jq --argjson value "$VAL" '.require_authentication = $value' "$AETHERCLI_CONFIG" > "$TMP_FILE" && mv "$TMP_FILE" "$AETHERCLI_CONFIG"; then
    :
else
    rm -f "$TMP_FILE"
    get_msg "script_cmd_error" "Error: Failed to modify config file."
    exit 1
fi

msg=$(get_msg "script_set_auth_success" "Authentication set to _VAL_. Changes will take effect in the next session.")
echo "${msg/_VAL_/$1}"
