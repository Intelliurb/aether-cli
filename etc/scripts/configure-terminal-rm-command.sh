#!/bin/bash
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$DIR/scripts/utils.sh"

if [ ! -f "$AETHERCLI_CONFIG" ]; then
    get_msg "script_config_not_found" "Error: config.json not found."
    exit 1
fi
check_write_permission "$AETHERCLI_CONFIG"

if [ "$#" -ne 1 ]; then
    get_msg "script_rm_cmd_usage" "Usage: rm command <command_path>"
    get_msg "script_rm_cmd_example" "  Example: rm command /show/ip"
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
  def exists($parts_left; $nodes):
    if ($parts_left | length) == 0 then true
    else any($nodes[]?; .name == $parts_left[0] and
      (if ($parts_left | length) == 1 then true
       else exists($parts_left[1:]; (.subcommands // [])) end))
    end;
  exists($parts; .commands)
' "$AETHERCLI_CONFIG")

if [ "$EXISTS" != "true" ]; then
    get_msg "script_rm_cmd_not_found" "Error: Command path '$CMD_PATH' not found."
    exit 1
fi

TMP_FILE=$(mktemp "$(dirname "$AETHERCLI_CONFIG")/.aethercli-config.XXXXXX") || exit 1

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

if [ $? -eq 0 ] && mv "$TMP_FILE" "$AETHERCLI_CONFIG"; then
    get_msg "script_cmd_removed" "Command removed successfully. Run 'reload conf' to apply."
else
    rm -f "$TMP_FILE"
    get_msg "script_cmd_error" "Error: Failed to modify config file."
fi
