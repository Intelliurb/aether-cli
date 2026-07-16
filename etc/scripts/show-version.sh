#!/bin/sh

DIR=$(dirname "$0")

# Procurar o binário aethercli no diretório de compilação ou no PATH
BIN="$(cd "$DIR/../../build" 2>/dev/null && pwd)/aethercli"
if ! command -v "$BIN" >/dev/null 2>&1; then
    BIN="aethercli"
fi

printf "AetherCLI Version: "
# O aethercli atualiza a saída de --version no próprio main.cpp
"$BIN" --version 2>/dev/null || echo "Unknown"
echo "---------------------------------"
echo "Operating System:"
uname -a
