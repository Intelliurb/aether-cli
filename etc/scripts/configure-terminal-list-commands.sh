#!/bin/bash
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
GLOBAL_CONFIG="$AETHERCLI_CONFIG"
USER_CONFIG_DIR="$HOME/.aethercli"
CONFIG_FILE="$USER_CONFIG_DIR/config.json"

if [ ! -f "$AETHERCLI_CONFIG" ]; then
    if [ -f "$GLOBAL_CONFIG" ]; then
        CONFIG_FILE="$GLOBAL_CONFIG"
    else
        echo "Error: config.json not found."
        exit 1
    fi
fi

echo "Available commands in $CONFIG_FILE:"

jq -r '
  def print_cmds($cmds; $prefix):
    ($cmds | type) as $t |
    if $t == "array" then
      .[] | print_cmds(.; $prefix)
    elif $t == "object" then
      ($prefix + "/" + .name) as $path |
      if .activation then
        "  \($path)  -  \(.short_desc)"
      else
        "  \($path)/  -  \(.short_desc // "...")"
      end,
      (if .subcommands then print_cmds(.subcommands; $path) else empty end)
    else empty end;
  
  if .commands then
    print_cmds(.commands; "")
  else empty end
' "$AETHERCLI_CONFIG"
