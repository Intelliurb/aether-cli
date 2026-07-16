#!/bin/bash
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$DIR/scripts/utils.sh"

USERS_FILE="$AETHERCLI_USERS_FILE"

if [ ! -f "$USERS_FILE" ]; then
    get_msg "script_users_none" "No users configured (users.json not found)."
    exit 0
fi

get_msg "script_users_list" "Configured users:"
jq -r '.users[] | .username' "$USERS_FILE" | while read -r user; do
    echo " - $user"
done
