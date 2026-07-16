#!/bin/bash
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$DIR/scripts/utils.sh"
check_write_permission "$AETHERCLI_CONFIG"

if [ "$#" -ne 1 ]; then
    get_msg "script_lang_usage" "Usage: set language <english-us|portuguese-brasil>"
    exit 1
fi

if [ "$1" = "english-us" ]; then
    VAL="en"
elif [ "$1" = "portuguese-brasil" ]; then
    VAL="pt"
else
    get_msg "script_lang_err" "Error: language must be 'english-us' or 'portuguese-brasil'."
    exit 1
fi

USER_CONFIG_DIR="$HOME/.aethercli"
mkdir -p "$USER_CONFIG_DIR"
echo "$VAL" > "$USER_CONFIG_DIR/language"

msg=$(get_msg "script_lang_success" "Language set to _VAL_. Changes will take effect in the next session.")
echo "${msg/_VAL_/$1}"
