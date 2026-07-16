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
    get_msg "script_rm_usage" "Usage: rm user <username>"
    exit 1
fi

if [ "$AETHERCLI_USER" != "admin" ]; then
    get_msg "script_admin_only" "% Error: Only the 'admin' user can perform this action."
    exit 1
fi

USERS_FILE="$DIR/users.json"

if [ ! -f "$USERS_FILE" ]; then
    get_msg "script_rm_none" "No users configured."
    exit 0
fi

USER=$1
TMP=$(mktemp)
jq ".users = [.users[] | select(.username != \"$USER\")]" "$USERS_FILE" > "$TMP" && mv "$TMP" "$USERS_FILE"

msg=$(get_msg "script_rm_success" "User '_USER_' removed (if existed).")
echo "${msg/_USER_/$USER}"
