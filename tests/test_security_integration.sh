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

# users.json compartilhado (fora de ~/.aethercli) não usa pepper por usuário.
[ ! -e "$HOME_DIR/.aethercli/pepper" ]

# Pepper por usuário: nasce com o users.json em ~/.aethercli, em modo 0400.
PEPPER_HOME="$TMP_DIR/pepper-home"
PEPPER_DIR="$PEPPER_HOME/.aethercli"
PEPPER_FILE="$PEPPER_DIR/pepper"
mkdir -p "$PEPPER_DIR"
PEPPER_CONFIG="$PEPPER_DIR/config.json"
printf '%s\n' '{"require_authentication":true,"commands":[{"name":"plain","activation":"echo PLAIN_RAN"}]}' > "$PEPPER_CONFIG"
printf 'senha12345\nsenha12345\n' | HOME="$PEPPER_HOME" "$BIN" --adduser operador >/dev/null
[ -n "$(find "$PEPPER_FILE" -type f -perm 400)" ]
[ "$(wc -c < "$PEPPER_FILE" | tr -d ' ')" -eq 64 ]
pepper_before=$(cat "$PEPPER_FILE")

output=$(printf 'operador\nsenha12345\n' | HOME="$PEPPER_HOME" "$BIN" -p plain 2>&1)
printf '%s\n' "$output" | grep -q 'PLAIN_RAN'

# O hash depende do pepper: o mesmo users.json sem o arquivo não autentica.
chmod 600 "$PEPPER_FILE"
mv "$PEPPER_FILE" "$PEPPER_FILE.bak"
set +e
output=$(printf 'operador\nsenha12345\n' | HOME="$PEPPER_HOME" "$BIN" -p plain 2>&1)
status=$?
set -e
[ "$status" -eq 1 ]
assert_absent "$output" 'PLAIN_RAN'
mv "$PEPPER_FILE.bak" "$PEPPER_FILE"

# Permissão aberta falha fechado, antes de pedir credenciais.
chmod 644 "$PEPPER_FILE"
set +e
output=$(printf 'operador\nsenha12345\n' | HOME="$PEPPER_HOME" "$BIN" -p plain 2>&1)
status=$?
set -e
[ "$status" -eq 1 ]
assert_absent "$output" 'PLAIN_RAN'
printf '%s\n' "$output" | grep -q 'pepper'
chmod 400 "$PEPPER_FILE"

# Novo --adduser reaproveita o pepper, sem invalidar quem já existe.
printf 'senha67890\nsenha67890\n' | HOME="$PEPPER_HOME" "$BIN" --adduser segundo >/dev/null
[ "$(cat "$PEPPER_FILE")" = "$pepper_before" ]
output=$(printf 'segundo\nsenha67890\n' | HOME="$PEPPER_HOME" "$BIN" -p plain 2>&1)
printf '%s\n' "$output" | grep -q 'PLAIN_RAN'

# Sem o pepper, o marcador do users.json impede gravar hash desprotegido.
users_before=$(cat "$PEPPER_DIR/users.json")
chmod 600 "$PEPPER_FILE"
mv "$PEPPER_FILE" "$PEPPER_FILE.bak"
set +e
printf 'senha11111\nsenha11111\n' | HOME="$PEPPER_HOME" "$BIN" --adduser terceiro >/dev/null 2>&1
status=$?
set -e
[ "$status" -eq 1 ]
[ "$(cat "$PEPPER_DIR/users.json")" = "$users_before" ]
[ ! -e "$PEPPER_FILE" ]

# FIFO, symlink, conteúdo inválido e modo 0600 são recusados sem travar.
pepper_rejected() {
    set +e
    output=$(printf 'operador\nsenha12345\n' | HOME="$PEPPER_HOME" "$BIN" -p plain 2>&1)
    status=$?
    set -e
    [ "$status" -eq 1 ]
    assert_absent "$output" 'PLAIN_RAN'
    printf '%s\n' "$output" | grep -q 'pepper'
}
mkfifo "$PEPPER_FILE"
pepper_rejected
rm -f "$PEPPER_FILE"
ln -s "$PEPPER_FILE.bak" "$PEPPER_FILE"
pepper_rejected
rm -f "$PEPPER_FILE"
printf 'curto' > "$PEPPER_FILE"
chmod 400 "$PEPPER_FILE"
pepper_rejected
rm -f "$PEPPER_FILE"
mv "$PEPPER_FILE.bak" "$PEPPER_FILE"
pepper_rejected
chmod 400 "$PEPPER_FILE"

# Variável de ambiente tem prioridade sobre o arquivo.
set +e
output=$(printf 'operador\nsenha12345\n' | AETHERCLI_PEPPER=outro HOME="$PEPPER_HOME" "$BIN" -p plain 2>&1)
set -e
assert_absent "$output" 'PLAIN_RAN'

# users.json legado em ~/.aethercli não ganha pepper: os hashes deixariam de valer.
LEGACY_HOME="$TMP_DIR/legacy-home"
mkdir -p "$LEGACY_HOME/.aethercli"
cp "$PEPPER_CONFIG" "$LEGACY_HOME/.aethercli/config.json"
printf 'senha12345\nsenha12345\n' | AETHERCLI_PEPPER=segredo HOME="$LEGACY_HOME" "$BIN" --adduser operador >/dev/null
[ ! -e "$LEGACY_HOME/.aethercli/pepper" ]
printf 'senha67890\nsenha67890\n' | HOME="$LEGACY_HOME" "$BIN" --adduser segundo >/dev/null
[ ! -e "$LEGACY_HOME/.aethercli/pepper" ]

# Senha inicial: aleatória, mostrada uma vez, só o hash em disco e sem sobrescrever.
INIT_HOME="$TMP_DIR/init-home"
mkdir -p "$INIT_HOME/.aethercli"
INIT_CONFIG="$INIT_HOME/.aethercli/config.json"
printf '%s\n' '{"require_authentication":true,"commands":[{"name":"danger","activation":"echo INIT_ADMIN_RAN","admin_only":true}]}' > "$INIT_CONFIG"
output=$(HOME="$INIT_HOME" AETHERCLI_BIN="$BIN" sh "$SCRIPTS_DIR/generate-initial-admin.sh" "$INIT_CONFIG")
init_password=$(printf '%s\n' "$output" | sed -n '$p')
[ "${#init_password}" -eq 24 ]
assert_absent "$(cat "$INIT_HOME/.aethercli/users.json")" "$init_password"
[ -n "$(find "$INIT_HOME/.aethercli/users.json" -type f -perm 600)" ]
[ -n "$(find "$INIT_HOME/.aethercli/pepper" -type f -perm 400)" ]
output=$(printf 'admin\n%s\n' "$init_password" | HOME="$INIT_HOME" "$BIN" -C "$INIT_CONFIG" -p danger 2>&1)
printf '%s\n' "$output" | grep -q 'INIT_ADMIN_RAN'
users_before=$(cat "$INIT_HOME/.aethercli/users.json")
set +e
HOME="$INIT_HOME" AETHERCLI_BIN="$BIN" sh "$SCRIPTS_DIR/generate-initial-admin.sh" "$INIT_CONFIG" >/dev/null 2>&1
status=$?
set -e
[ "$status" -eq 1 ]
[ "$(cat "$INIT_HOME/.aethercli/users.json")" = "$users_before" ]

# O template não pode servir de credencial: copiado como está, nega o login.
TEMPLATE_HOME="$TMP_DIR/template-home"
mkdir -p "$TEMPLATE_HOME/.aethercli"
TEMPLATE_CONFIG="$TEMPLATE_HOME/.aethercli/config.json"
printf '%s\n' '{"require_authentication":true,"commands":[{"name":"plain","activation":"echo TEMPLATE_RAN"}]}' > "$TEMPLATE_CONFIG"
cp "$SCRIPTS_DIR/../users.json.template" "$TEMPLATE_HOME/.aethercli/users.json"
set +e
output=$(printf 'admin\nsenha12345\n' | HOME="$TEMPLATE_HOME" "$BIN" -C "$TEMPLATE_CONFIG" -p plain 2>&1)
status=$?
set -e
[ "$status" -eq 1 ]
assert_absent "$output" 'TEMPLATE_RAN'

# "add user" no bootstrap também gera o pepper (o teste de escrita não cria o arquivo).
ADD_HOME="$TMP_DIR/add home"
mkdir -p "$ADD_HOME/.aethercli"
ADD_CONFIG="$ADD_HOME/.aethercli/config.json"
cp "$TEMPLATE_CONFIG" "$ADD_CONFIG"
printf 'senha12345\nsenha12345\n' | PATH="$(dirname "$BIN"):$PATH" HOME="$ADD_HOME" AETHERCLI_USER=admin \
    AETHERCLI_CONFIG="$ADD_CONFIG" AETHERCLI_USERS_FILE="$ADD_HOME/.aethercli/users.json" \
    bash "$SCRIPTS_DIR/configure-terminal-add-user.sh" operador >/dev/null
[ -n "$(find "$ADD_HOME/.aethercli/pepper" -type f -perm 400)" ]
[ "$(jq -r '.pepper_file' "$ADD_HOME/.aethercli/users.json")" = "true" ]

# Arquivo marcado, fora do HOME do pepper: recusa em vez de gravar hash sem pepper.
users_before=$(cat "$ADD_HOME/.aethercli/users.json")
set +e
printf 'senha11111\nsenha11111\n' | HOME="$TMP_DIR/outro-home" "$BIN" -C "$ADD_CONFIG" --adduser terceiro >/dev/null 2>&1
status=$?
set -e
[ "$status" -eq 1 ]
[ "$(cat "$ADD_HOME/.aethercli/users.json")" = "$users_before" ]

# Gerador em caminho com espaço e com passwd-file vazio.
SPACE_DIR="$TMP_DIR/init space"
mkdir -p "$SPACE_DIR"
printf '%s\n' '{"passwd-file":"","commands":[]}' > "$SPACE_DIR/config.json"
output=$(HOME="$TMP_DIR/outro-home" AETHERCLI_BIN="$BIN" sh "$SCRIPTS_DIR/generate-initial-admin.sh" "$SPACE_DIR/config.json")
[ "$(printf '%s\n' "$output" | sed -n '$p' | wc -c | tr -d ' ')" -eq 25 ]
[ -n "$(find "$SPACE_DIR/users.json" -type f -perm 600)" ]
