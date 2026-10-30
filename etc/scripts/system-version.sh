#!/bin/sh

DIR=$(dirname "$0")
. "$DIR/utils.sh"

# Procurar o binário aethercli no diretório de compilação ou no PATH
BIN="$(cd "$DIR/../../build" 2>/dev/null && pwd)/aethercli"
if ! command -v "$BIN" >/dev/null 2>&1; then
    BIN="aethercli"
fi

printf '%s ' "$(get_msg "script_version_title" "AetherCLI Version:")"
# O aethercli atualiza a saída de --version no próprio main.cpp
"$BIN" --version 2>/dev/null || get_msg "script_version_unknown" "Unknown"
get_msg "script_separator" "---------------------------------"
get_msg "script_os_title" "Operating System:"
uname -a
