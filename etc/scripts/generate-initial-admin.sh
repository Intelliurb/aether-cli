#!/bin/sh
# Cria o usuário admin com uma senha aleatória e a mostra uma única vez.
# Uso: generate-initial-admin.sh [config.json]
# A senha não passa por argv nem é gravada em arquivo; só o hash fica no users.json.
AETHERCLI_CONFIG=${1:-${AETHERCLI_CONFIG:-}}
DIR=$(dirname "$0")
. "$DIR/utils.sh"

if [ ! -f "$AETHERCLI_CONFIG" ]; then
    get_msg "script_config_not_found" "Error: config.json not found." >&2
    exit 1
fi

CONFIG_DIR=$(cd "$(dirname "$AETHERCLI_CONFIG")" && pwd) || exit 1
USERS_FILE=$(jq -r '(.["passwd-file"] // "") | if . == "" then "users.json" else . end' "$AETHERCLI_CONFIG") || exit 1
case "$USERS_FILE" in
    /*) ;;
    *) USERS_FILE="$CONFIG_DIR/$USERS_FILE" ;;
esac

# Nunca substitui contas: a senha inicial só existe enquanto não há usuários.
if [ -e "$USERS_FILE" ]; then
    get_msg "script_initpw_exists" "Error: users file already exists; initial password not generated." >&2
    exit 1
fi

# 24 caracteres de um alfabeto de 62: cerca de 142 bits de entropia.
PASSWORD=$(LC_ALL=C tr -dc 'A-Za-z0-9' < /dev/urandom | dd bs=24 count=1 2>/dev/null)
if [ "${#PASSWORD}" -ne 24 ]; then
    get_msg "script_initpw_random" "Error: could not generate a random password." >&2
    exit 1
fi

BIN=${AETHERCLI_BIN:-aethercli}
if ! printf '%s\n%s\n' "$PASSWORD" "$PASSWORD" | "$BIN" -C "$AETHERCLI_CONFIG" --adduser admin >/dev/null; then
    PASSWORD=
    get_msg "script_initpw_failed" "Error: could not create the admin user." >&2
    exit 1
fi

get_msg "script_initpw_created" "Admin user created. Save this password now; it will not be shown again:"
# Sem saída a senha se perderia com a conta já criada: o chamador precisa saber.
if ! printf '%s\n' "$PASSWORD"; then
    PASSWORD=
    exit 1
fi
PASSWORD=
