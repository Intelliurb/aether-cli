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

if [ "$#" -ne 1 ]; then
    get_msg "script_rm_cmd_usage" "Usage: rm command <command_path>"
    echo "  Example: rm command /show/ip"
    exit 1
fi

CMD_PATH="$1"

if [[ "$CMD_PATH" == "/configure" || "$CMD_PATH" == "/configure/"* ]]; then
    get_msg "script_cmd_restricted" "Error: The /configure tree is restricted and cannot be modified."
    exit 1
fi

if [[ "$CMD_PATH" == "/" || "$CMD_PATH" == "" ]]; then
    get_msg "script_cmd_root_rm" "Error: Cannot remove the root."
    exit 1
fi

EXISTS=$(jq -r --arg path "$CMD_PATH" '
  ($path | sub("^/"; "") | split("/")) as $parts |
  def check_exists($parts_left; $node):
    if ($parts_left | length) == 1 then
      if ($node | type) == "array" then
        any($node[]; .name == $parts_left[0])
      else
        any(.subcommands[]?; .name == $parts_left[0])
      end
    else
      if ($node | type) == "array" then
        if any($node[]; .name == $parts_left[0]) then
          check_exists($parts_left[1:]; ($node | map(select(.name == $parts_left[0]))[0]))
        else false end
      else
        if any(.subcommands[]?; .name == $parts_left[0]) then
          check_exists($parts_left[1:]; (.subcommands | map(select(.name == $parts_left[0]))[0]))
        else false end
      end
    end;
  check_exists($parts; .commands)
' "$AETHERCLI_CONFIG")

if [ "$EXISTS" != "true" ]; then
    get_msg "script_rm_cmd_not_found" "Error: Command path '$CMD_PATH' not found."
    exit 1
fi

TMP_FILE=$(mktemp)

jq --arg path "$CMD_PATH" '
  ($path | sub("^/"; "") | split("/")) as $parts |
  def rm_cmd($parts_left; $node):
    if ($parts_left | length) == 1 then
      if ($node | type) == "array" then
        map(select(.name != $parts_left[0]))
      else
        .subcommands = (.subcommands | map(select(.name != $parts_left[0])))
      end
    else
      if ($node | type) == "array" then
        map(if .name == $parts_left[0] then rm_cmd($parts_left[1:]; .) else . end)
      else
        .subcommands = (.subcommands | map(if .name == $parts_left[0] then rm_cmd($parts_left[1:]; .) else . end))
      end
    end;
  .commands = (.commands | rm_cmd($parts; .))
' "$AETHERCLI_CONFIG" > "$TMP_FILE"

if [ $? -eq 0 ]; then
    cat "$TMP_FILE" > "$AETHERCLI_CONFIG"
    get_msg "script_cmd_removed" "Command removed successfully. Run 'reload conf' to apply."
else
    get_msg "script_cmd_error" "Error: Failed to modify config file."
fi
rm -f "$TMP_FILE"
