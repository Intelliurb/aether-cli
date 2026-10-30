#!/bin/sh
set -eu

BIN=$1
# "! cmd" não aborta sob set -e: a ausência precisa de uma falha explícita.
assert_absent() {
    if printf '%s\n' "$1" | grep -q "$2"; then
        echo "FALHA: saída contém '$2': $1" >&2
        exit 1
    fi
}

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
assert_absent "$output" 'HOME'

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
assert_absent "$output" 'SHOULD_NOT_RUN'

set +e
output=$(printf 'operador\nsenha12345\n' | HOME="$HOME_DIR" "$BIN" -C "$AUTH_CONFIG" -p danger 2>&1)
status=$?
set -e
[ "$status" -eq 1 ]
printf '%s\n' "$output" | grep -q "Only the 'admin' user"
assert_absent "$output" 'SHOULD_NOT_RUN'

# Identidade alegada no ambiente não substitui o login.
set +e
output=$(printf '' | AETHERCLI_USER=admin HOME="$HOME_DIR" "$BIN" -C "$AUTH_CONFIG" -p danger 2>&1)
status=$?
set -e
[ "$status" -eq 1 ]
assert_absent "$output" 'SHOULD_NOT_RUN'

# admin_only no pai vale para os filhos.
PARENT_CONFIG="$TMP_DIR/parent.json"
printf '%s\n' '{"commands":[{"name":"parent","admin_only":true,"subcommands":[{"name":"child","activation":"echo CHILD_RAN"}]}]}' > "$PARENT_CONFIG"
set +e
output=$(AETHERCLI_USER=operador HOME="$HOME_DIR" "$BIN" -C "$PARENT_CONFIG" -p 'parent child' 2>&1)
status=$?
set -e
[ "$status" -eq 1 ]
assert_absent "$output" 'CHILD_RAN'

# O log contém argumentos: não pode nascer legível por outros usuários.
HOME="$HOME_DIR" "$BIN" --log "$TMP_DIR/run.log" -C "$EXPLICIT_CONFIG" -p 'show marker' >/dev/null
[ -z "$(find "$TMP_DIR/run.log" -perm -004 -o -perm -040)" ]

# Nome de usuário em "rm user" é dado, não programa jq.
SCRIPTS_DIR=$(cd "$(dirname "$0")/../etc/scripts" && pwd)
printf '%s\n' '{"users":[{"username":"a"},{"username":"b"}]}' > "$TMP_DIR/users.json"
AETHERCLI_USER=admin AETHERCLI_CONFIG="$EXPLICIT_CONFIG" AETHERCLI_USERS_FILE="$TMP_DIR/users.json" \
    bash "$SCRIPTS_DIR/configure-terminal-rm-user.sh" 'x" or true) | select(false or "a"=="b' >/dev/null
[ "$(jq '.users | length' "$TMP_DIR/users.json")" -eq 2 ]
AETHERCLI_USER=admin AETHERCLI_CONFIG="$EXPLICIT_CONFIG" AETHERCLI_USERS_FILE="$TMP_DIR/users.json" \
    bash "$SCRIPTS_DIR/configure-terminal-rm-user.sh" a >/dev/null
[ "$(jq -r '.users[0].username' "$TMP_DIR/users.json")" = "b" ]
