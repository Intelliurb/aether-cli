#!/bin/sh
set -eu

BIN=$1
TMP_DIR=$(mktemp -d)
trap 'rm -rf "$TMP_DIR"' EXIT HUP INT TERM
HOME_DIR="$TMP_DIR/home"
mkdir -p "$HOME_DIR/.aethercli"

EXPLICIT_CONFIG="$TMP_DIR/explicit.json"
HOME_CONFIG="$HOME_DIR/.aethercli/config.json"
AUTH_CONFIG="$TMP_DIR/auth.json"
BOOTSTRAP_CONFIG="$TMP_DIR/bootstrap.json"

printf '%s\n' '{"commands":[{"name":"show","subcommands":[{"name":"marker","activation":"echo EXPLICIT"}]}]}' > "$EXPLICIT_CONFIG"
printf '%s\n' '{"commands":[{"name":"show","subcommands":[{"name":"marker","activation":"echo HOME"}]}]}' > "$HOME_CONFIG"

output=$(HOME="$HOME_DIR" "$BIN" -C "$EXPLICIT_CONFIG" -p 'show marker')
printf '%s\n' "$output" | grep -q 'EXPLICIT'
! printf '%s\n' "$output" | grep -q 'HOME'

printf '%s\n' '{"require_authentication":true,"commands":[{"name":"danger","activation":"echo SHOULD_NOT_RUN","admin_only":true}]}' > "$AUTH_CONFIG"
printf '%s\n' '{"require_authentication":true,"passwd-file":"bootstrap-users.json","commands":[{"name":"danger","activation":"echo BOOTSTRAP_ADMIN","admin_only":true}]}' > "$BOOTSTRAP_CONFIG"
printf '%s\n' false > "$HOME_DIR/.aethercli/authentication"

output=$(HOME="$HOME_DIR" "$BIN" -C "$BOOTSTRAP_CONFIG" -p danger)
printf '%s\n' "$output" | grep -q 'BOOTSTRAP_ADMIN'

printf 'senha12345\nsenha12345\n' | HOME="$HOME_DIR" "$BIN" -C "$AUTH_CONFIG" --adduser operador >/dev/null

set +e
output=$(printf '' | HOME="$HOME_DIR" "$BIN" -C "$AUTH_CONFIG" -p danger 2>&1)
status=$?
set -e
[ "$status" -eq 1 ]
! printf '%s\n' "$output" | grep -q 'SHOULD_NOT_RUN'

set +e
output=$(printf 'operador\nsenha12345\n' | HOME="$HOME_DIR" "$BIN" -C "$AUTH_CONFIG" -p danger 2>&1)
status=$?
set -e
[ "$status" -eq 0 ]
printf '%s\n' "$output" | grep -q "Only the 'admin' user"
! printf '%s\n' "$output" | grep -q 'SHOULD_NOT_RUN'
