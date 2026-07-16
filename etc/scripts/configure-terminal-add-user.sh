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
    get_msg "script_add_usage" "Usage: add user <username>"
    exit 1
fi

if [ "$AETHERCLI_USER" != "admin" ]; then
    get_msg "script_admin_only" "% Error: Only the 'admin' user can perform this action."
    exit 1
fi

BIN="$(cd "$(dirname "${BASH_SOURCE[0]}")/../build" && pwd)/aethercli"
if ! command -v "$BIN" >/dev/null 2>&1; then
    BIN="aethercli"
fi

exec "$BIN" -C "$DIR/config.json" --adduser "$1"
