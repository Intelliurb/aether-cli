#!/bin/sh

DIR=$(dirname "$0")
. "$DIR/utils.sh"

# handled by utils.sh

TITLE=$(get_msg "show_config_title" "System Configuration")
echo "$TITLE ($AETHERCLI_CONFIG)"
echo "=============================================================="

jq -r 'del(.commands) | to_entries[] | "\(.key | (tostring + "                        ")[0:25]): \(.value | tostring | gsub("\n"; "\n                          "))"' "$AETHERCLI_CONFIG"
