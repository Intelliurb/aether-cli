#!/bin/sh
# Scripts should work in pure sh.

DIR=$(dirname "$0")
. "$DIR/utils.sh"

# handled by utils.sh

jq -r '
def print_tree:
  .value as $node
  | .prefix as $prefix
  | .is_last as $is_last
  | (if $is_last then $prefix + "└── " else $prefix + "├── " end) as $curr_prefix
  | (if $is_last then $prefix + "    " else $prefix + "│   " end) as $next_prefix
  | ($curr_prefix + $node.name) ,
    (if $node.subcommands then
       ( $node.subcommands | length ) as $len |
       $node.subcommands | to_entries[] |
       {value: .value, prefix: $next_prefix, is_last: (.key == $len - 1)} |
       print_tree
     else empty end);

"commands", (.commands | (length) as $len | to_entries[] | {value: .value, prefix: "", is_last: (.key == $len - 1)} | print_tree)
' "$AETHERCLI_CONFIG"
