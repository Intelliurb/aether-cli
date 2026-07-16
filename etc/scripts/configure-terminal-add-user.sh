#!/bin/bash
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$DIR/scripts/utils.sh"
check_write_permission "$AETHERCLI_USERS_FILE"

if [ "$#" -ne 1 ]; then
    get_msg "script_add_usage" "Usage: add user <username>"
    exit 1
fi

if [ "$AETHERCLI_USER" != "admin" ]; then
    get_msg "script_admin_only" "% Error: Only the 'admin' user can perform this action."
    exit 1
fi

BIN="$(cd "$(dirname "${BASH_SOURCE[0]}")/../build" 2>/dev/null && pwd)/aethercli"
if ! command -v "$BIN" >/dev/null 2>&1; then
    BIN="aethercli"
fi

exec "$BIN" -C "$AETHERCLI_CONFIG" --adduser "$1"
