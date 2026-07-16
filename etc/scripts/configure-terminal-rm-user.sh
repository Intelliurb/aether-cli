#!/bin/bash
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$DIR/scripts/utils.sh"
check_write_permission "$AETHERCLI_USERS_FILE"

if [ "$#" -ne 1 ]; then
    get_msg "script_rm_usage" "Usage: rm user <username>"
    exit 1
fi

if [ "$AETHERCLI_USER" != "admin" ]; then
    get_msg "script_admin_only" "% Error: Only the 'admin' user can perform this action."
    exit 1
fi

USERS_FILE="$AETHERCLI_USERS_FILE"

if [ ! -f "$USERS_FILE" ]; then
    get_msg "script_rm_none" "No users configured."
    exit 0
fi

USER=$1
TMP=$(mktemp)
jq ".users = [.users[] | select(.username != \"$USER\")]" "$USERS_FILE" > "$TMP" && mv "$TMP" "$USERS_FILE"

msg=$(get_msg "script_rm_success" "User '_USER_' removed (if existed).")
echo "${msg/_USER_/$USER}"
