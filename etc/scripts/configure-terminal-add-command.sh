#!/bin/bash
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
GLOBAL_CONFIG="$AETHERCLI_CONFIG"
USER_CONFIG_DIR="$HOME/.aethercli"
CONFIG_FILE="$USER_CONFIG_DIR/config.json"

mkdir -p "$USER_CONFIG_DIR"
if [ ! -f "$AETHERCLI_CONFIG" ]; then
    if [ -f "$GLOBAL_CONFIG" ]; then
        cp "$GLOBAL_CONFIG" "$AETHERCLI_CONFIG"
    else
        echo "Error: config.json not found."
        exit 1
    fi
fi
source "$DIR/scripts/utils.sh"
check_write_permission "$AETHERCLI_CONFIG"

if [ "$#" -lt 3 ]; then
    get_msg "script_add_cmd_usage" "Usage: add command <parent_path> <name> <short_desc> [activation] [syntax]"
    echo "  Use / for root level. Use quotes if arguments contain spaces."
    echo "  Example: add command /show \"disk\" \"Show disk usage\" \"df -h\" \"show disk\""
    exit 1
fi

PARENT_PATH="$1"
NAME="$2"
SHORT_DESC="$3"
ACTIVATION="${4:-}"
SYNTAX="${5:-}"

if [[ "$PARENT_PATH" == "/configure" || "$PARENT_PATH" == "/configure/"* ]]; then
    get_msg "script_cmd_restricted" "Error: The /configure tree is restricted and cannot be modified."
    exit 1
fi

NEW_CMD=$(jq -n \
  --arg name "$NAME" \
  --arg short_desc "$SHORT_DESC" \
  --arg activation "$ACTIVATION" \
  --arg syntax "$SYNTAX" \
  '{
    name: $name,
    short_desc: $short_desc
  } + (if $activation != "" then {activation: $activation} else {} end)
    + (if $syntax != "" then {syntax: $syntax} else {} end)')

TMP_FILE=$(mktemp)

jq --arg path "$PARENT_PATH" --argjson new_cmd "$NEW_CMD" '
  ($path | sub("^/"; "") | split("/")) as $parts |
  def upsert(arr; cmd):
    if any(arr[]; .name == cmd.name) then
      arr | map(if .name == cmd.name then cmd else . end)
    else
      arr + [cmd]
    end;
  def insert_cmd($parts_left; $node):
    if ($parts_left | length) == 0 or ($parts_left | length == 1 and $parts_left[0] == "") then
      if ($node | type) == "array" then
        upsert($node; $new_cmd)
      else
        $node | .subcommands = upsert((.subcommands // []); $new_cmd)
      end
    else
      if ($node | type) == "array" then
        map(if .name == $parts_left[0] then insert_cmd($parts_left[1:]; .) else . end)
      else
        .subcommands = (.subcommands | map(if .name == $parts_left[0] then insert_cmd($parts_left[1:]; .) else . end))
      end
    end;
  if $path == "/" or $path == "" then
    .commands = upsert(.commands; $new_cmd)
  else
    .commands = (.commands | insert_cmd($parts; .))
  end
' "$AETHERCLI_CONFIG" > "$TMP_FILE"

if [ $? -eq 0 ]; then
    cat "$TMP_FILE" > "$AETHERCLI_CONFIG"
    get_msg "script_cmd_added" "Command added successfully. Run 'reload conf' to apply."
else
    get_msg "script_cmd_error" "Error: Failed to modify config file."
fi
rm -f "$TMP_FILE"
