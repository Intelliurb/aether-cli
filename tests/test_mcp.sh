#!/bin/sh
# Testes de conformidade do servidor MCP e modo headless (-p) do aethercli
# Cobertura: HL-1, MCP-1, MCP-2, MCP-V, MCP-T
set -eu

BIN=$1
if [ ! -x "$BIN" ]; then
    echo "Erro: binário '$BIN' não encontrado ou não executável" >&2
    exit 1
fi

# "! cmd" não aborta sob set -e: a ausência precisa de uma falha explícita.
assert_absent() {
    if printf '%s\n' "$1" | grep -q "$2"; then
        echo "FALHA: saída contém '$2': $1" >&2
        exit 1
    fi
}

TMP_DIR=$(mktemp -d)
trap 'rm -rf "$TMP_DIR"' EXIT HUP INT TERM
# HOME próprio: ~/.aethercli/language do usuário mudaria as mensagens conferidas.
HOME="$TMP_DIR/home"
export HOME
mkdir -p "$HOME"

SCRIPT_DIR=$(cd "$(dirname "$0")/.." && pwd)
if [ -f "$SCRIPT_DIR/etc/lang_en.json" ]; then
    cp "$SCRIPT_DIR/etc/lang_en.json" "$TMP_DIR/"
    cp "$SCRIPT_DIR/etc/lang_pt.json" "$TMP_DIR/"
fi

CONFIG_FILE="$TMP_DIR/mcp_test_config.json"
cat << 'EOF' > "$CONFIG_FILE"
{
  "config_version": "1.0",
  "require_authentication": false,
  "commands": [
    {
      "name": "echo_test",
      "short_desc": "Echo args back",
      "activation": "printf 'ECHO:%s\n'",
      "args": "append",
      "syntax": "echo_test <message>"
    },
    {
      "name": "no_args_cmd",
      "short_desc": "Command without arguments",
      "activation": "echo 'NO_ARGS_OK'",
      "args": "none"
    },
    {
      "name": "fail_exit3",
      "short_desc": "Command that exits with status 3",
      "activation": "sh -c 'printf \"FAILED_WITH_3\"; exit 3'"
    },
    {
      "name": "signaled_cmd",
      "short_desc": "Command terminated by signal 15",
      "activation": "sh -c 'kill -15 $$'"
    },
    {
      "name": "timeout_cmd",
      "short_desc": "Command with 2s timeout running sleep 30",
      "activation": "sleep 30",
      "timeout_s": 2
    },
    {
      "name": "trunc_cmd",
      "short_desc": "Command exceeding max output bytes",
      "activation": "sh -c 'printf \"012345678901234567890123456789\"'",
      "max_output_bytes": 10
    },
    {
      "name": "stdin_reader",
      "short_desc": "Command that reads from stdin",
      "activation": "sh -c 'if read -r line; then echo \"GOT:$line\"; else echo \"GOT_EOF\"; fi'"
    },
    {
      "name": "env_cwd_cmd",
      "short_desc": "Command verifying cwd and env",
      "activation": "sh -c 'printf \"CWD=%s;ENV=%s\n\" \"$(pwd)\" \"$TEST_CUSTOM_VAR\"'",
      "cwd": "/tmp",
      "env": {
        "TEST_CUSTOM_VAR": "aethercli_val_42"
      }
    },
    {
      "name": "typed_cmd",
      "short_desc": "Typed test command",
      "activation": "sh -c 'printf \"ARG1=%s;ENV_MODE=%s;ENV_COUNT=%s;ENV_FLAG=%s\n\" \"$1\" \"$AETHERCLI_PARAM_MODE\" \"$AETHERCLI_PARAM_COUNT\" \"$AETHERCLI_PARAM_FLAG\"' --",
      "effect": "read",
      "idempotent": true,
      "open_world": false,
      "params": [
        {
          "name": "mode",
          "type": "string",
          "description": "Operating mode",
          "required": true,
          "enum": ["fast", "safe"]
        },
        {
          "name": "count",
          "type": "integer",
          "description": "Repetition count",
          "required": true,
          "minimum": 1,
          "maximum": 10
        },
        {
          "name": "flag",
          "type": "boolean",
          "description": "Verbose flag",
          "required": false
        }
      ]
    },
    {
      "name": "destructive_cmd",
      "short_desc": "Destructive command",
      "activation": "echo DESTRUCTIVE_OK",
      "effect": "destructive",
      "idempotent": false
    },
    {
      "name": "admin_cmd",
      "short_desc": "Admin only command",
      "activation": "echo ADMIN_OK",
      "admin_only": true
    }
  ]
}
EOF

echo "=== Testando HL-1 (caminho headless -p) ==="

# 1.1 ativação exit 3 termina com status 3
set +e
"$BIN" -C "$CONFIG_FILE" -p "fail_exit3" >/dev/null 2>&1
status=$?
set -e
if [ "$status" -ne 3 ]; then
    echo "FALHA HL-1: fail_exit3 retornou status $status, esperado 3" >&2
    exit 1
fi
echo "  [OK] -p fail_exit3 retornou 3"

# 1.2 ativação sinalizada termina com 128 + sinal (128 + 15 = 143)
set +e
"$BIN" -C "$CONFIG_FILE" -p "signaled_cmd" >/dev/null 2>&1
status=$?
set -e
if [ "$status" -ne 143 ]; then
    echo "FALHA HL-1: signaled_cmd retornou status $status, esperado 143" >&2
    exit 1
fi
echo "  [OK] -p signaled_cmd retornou 143 (128+15)"

# 1.3 comando desconhecido termina com status diferente de 0
set +e
"$BIN" -C "$CONFIG_FILE" -p "comando_inexistente" >/dev/null 2>&1
status=$?
set -e
if [ "$status" -eq 0 ]; then
    echo "FALHA HL-1: comando_inexistente retornou status 0, esperado != 0" >&2
    exit 1
fi
echo "  [OK] -p comando_inexistente retornou != 0 ($status)"

# 1.4 timeout em -p termina em menos de 4s com status 124
t_start=$(date +%s)
set +e
"$BIN" -C "$CONFIG_FILE" -p "timeout_cmd" >/dev/null 2>&1
status=$?
set -e
t_end=$(date +%s)
t_elapsed=$((t_end - t_start))
if [ "$t_elapsed" -ge 4 ]; then
    echo "FALHA MCP-2 (-p): timeout demorou ${t_elapsed}s, esperado < 4s" >&2
    exit 1
fi
if [ "$status" -ne 124 ]; then
    echo "FALHA MCP-2 (-p): timeout_cmd retornou status $status, esperado 124" >&2
    exit 1
fi
echo "  [OK] -p timeout_cmd terminou em ${t_elapsed}s com status 124"

# 1.5 cwd e env chegam ao filho em -p
output=$("$BIN" -C "$CONFIG_FILE" -p "env_cwd_cmd")
if ! printf '%s\n' "$output" | grep -q "CWD=/tmp;ENV=aethercli_val_42"; then
    # /tmp no macOS pode resolver para /private/tmp
    if ! printf '%s\n' "$output" | grep -q "ENV=aethercli_val_42"; then
        echo "FALHA MCP-2 (-p): env_cwd_cmd não recebeu cwd/env esperado. Saída: $output" >&2
        exit 1
    fi
fi
echo "  [OK] -p env_cwd_cmd recebeu cwd e env"

# 1.6 -p typed_cmd com parâmetros válidos repassa argumentos posicionais e env vars
output=$("$BIN" -C "$CONFIG_FILE" -p "typed_cmd fast 5 true")
if ! printf '%s\n' "$output" | grep -q "ARG1=fast;ENV_MODE=fast;ENV_COUNT=5;ENV_FLAG=true"; then
    echo "FALHA PAR-2 (-p): typed_cmd não recebeu parâmetros esperados. Saída: $output" >&2
    exit 1
fi
echo "  [OK] -p typed_cmd repassou parâmetros posicionais e env vars"

# 1.7 -p typed_cmd com enum inválido é rejeitado com exit != 0
set +e
output=$("$BIN" -C "$CONFIG_FILE" -p "typed_cmd invalid_mode 5" 2>&1)
status=$?
set -e
if [ "$status" -eq 0 ]; then
    echo "FALHA PAR-2 (-p): typed_cmd aceitou enum inválido com status 0" >&2
    exit 1
fi
if ! printf '%s\n' "$output" | grep -qi "must be one of"; then
    echo "FALHA PAR-2 (-p): typed_cmd enum inválido sem mensagem apropriada: $output" >&2
    exit 1
fi
echo "  [OK] -p typed_cmd enum inválido rejeitado com status != 0"

# 1.8 -p typed_cmd com integer fora de faixa (máximo 10) é rejeitado
set +e
output=$("$BIN" -C "$CONFIG_FILE" -p "typed_cmd fast 99" 2>&1)
status=$?
set -e
if [ "$status" -eq 0 ]; then
    echo "FALHA PAR-2 (-p): typed_cmd aceitou valor 99 > max 10 com status 0" >&2
    exit 1
fi
echo "  [OK] -p typed_cmd integer fora de faixa rejeitado"

# 1.9 -p typed_cmd faltando parâmetro obrigatório é rejeitado
set +e
output=$("$BIN" -C "$CONFIG_FILE" -p "typed_cmd fast" 2>&1)
status=$?
set -e
if [ "$status" -eq 0 ]; then
    echo "FALHA PAR-2 (-p): typed_cmd aceitou chamada faltando argumento obrigatório" >&2
    exit 1
fi
echo "  [OK] -p typed_cmd faltando parâmetro obrigatório rejeitado"

# 1.10 -p proteção de injeção em parâmetro string
# Criar comando para teste de injeção segura de string
INJ_CONFIG="$TMP_DIR/inj_config.json"
cat << 'EOF' > "$INJ_CONFIG"
{
  "config_version": "1.0",
  "require_authentication": false,
  "commands": [
    {
      "name": "safe_echo",
      "short_desc": "Safe echo",
      "activation": "sh -c 'printf \"SAFE_ARG=%s\n\" \"$1\"' --",
      "params": [
        { "name": "val", "type": "string", "description": "Value", "required": true }
      ]
    }
  ]
}
EOF
output=$("$BIN" -C "$INJ_CONFIG" -p 'safe_echo "foo; touch /tmp/should_not_exist_aethercli"' 2>&1 || true)
if [ -f "/tmp/should_not_exist_aethercli" ]; then
    rm -f "/tmp/should_not_exist_aethercli"
    echo "FALHA PAR-2 (-p): Injeção de shell executada através de parâmetro!" >&2
    exit 1
fi
if ! printf '%s\n' "$output" | grep -q 'SAFE_ARG=foo; touch /tmp/should_not_exist_aethercli'; then
    echo "FALHA PAR-2 (-p): Argumento com ponto-e-vírgula não foi repassado literalmente de forma segura: $output" >&2
    exit 1
fi
echo "  [OK] -p metacaracteres em parâmetros escapados com segurança literal"



echo "=== Testando MCP-V e MCP-1 (Servidor MCP) ==="

# Helper para executar uma sessão MCP fornecendo entrada e coletando linhas de saída
run_mcp_session() {
    input_data=$1
    printf '%s\n' "$input_data" | "$BIN" -C "$CONFIG_FILE" --mcp
}

# 2.1 server/discover obrigatório na era moderna (2026-07-28)
req='{"jsonrpc": "2.0", "id": 1, "method": "server/discover"}'
res=$(run_mcp_session "$req")
printf '%s\n' "$res" | jq -e '.result.supportedVersions == ["2026-07-28", "2025-11-25"]' >/dev/null
printf '%s\n' "$res" | jq -e '.result.resultType == "complete"' >/dev/null
printf '%s\n' "$res" | jq -e '.result.ttlMs == 300000' >/dev/null
printf '%s\n' "$res" | jq -e '.result.cacheScope == "public"' >/dev/null
printf '%s\n' "$res" | jq -e '.result._meta["io.modelcontextprotocol/serverInfo"].name == "aethercli-mcp"' >/dev/null
printf '%s\n' "$res" | jq -e '.result._meta["io.modelcontextprotocol/serverInfo"].version == "0.7.1"' >/dev/null
echo "  [OK] server/discover responde conforme 2026-07-28"

# 2.2 Versão não suportada em _meta retorna UnsupportedProtocolVersionError (-32022)
req='{"jsonrpc": "2.0", "id": 2, "method": "tools/list", "params": {"_meta": {"io.modelcontextprotocol/protocolVersion": "1999-01-01"}}}'
res=$(run_mcp_session "$req")
printf '%s\n' "$res" | jq -e '.error.code == -32022' >/dev/null
printf '%s\n' "$res" | jq -e '.error.message == "Unsupported protocol version"' >/dev/null
printf '%s\n' "$res" | jq -e '.error.data.supported == ["2026-07-28", "2025-11-25"]' >/dev/null
printf '%s\n' "$res" | jq -e '.error.data.requested == "1999-01-01"' >/dev/null
echo "  [OK] UnsupportedProtocolVersionError (-32022) gerado corretamente"

# 2.3 Sessão legada que inicia com initialize negocia 2025-11-25
req='{"jsonrpc": "2.0", "id": 3, "method": "initialize", "params": {"protocolVersion": "2025-11-25"}}'
res=$(run_mcp_session "$req")
printf '%s\n' "$res" | jq -e '.result.protocolVersion == "2025-11-25"' >/dev/null
printf '%s\n' "$res" | jq -e '.result.serverInfo.name == "aethercli-mcp"' >/dev/null
printf '%s\n' "$res" | jq -e '.result.serverInfo.version == "0.7.1"' >/dev/null
echo "  [OK] initialize negocia versão legada 2025-11-25"

# 2.4 tools/list ordem determinística e inputSchema correto
req1='{"jsonrpc": "2.0", "id": 4, "method": "tools/list"}'
req2='{"jsonrpc": "2.0", "id": 5, "method": "tools/list"}'
res_both=$(printf '%s\n%s\n' "$req1" "$req2" | "$BIN" -C "$CONFIG_FILE" --mcp)
res1=$(printf '%s\n' "$res_both" | sed -n '1p')
res2=$(printf '%s\n' "$res_both" | sed -n '2p')

# Comparar a ordem dos nomes das ferramentas em ambas as chamadas
names1=$(printf '%s\n' "$res1" | jq -c '[.result.tools[].name]')
names2=$(printf '%s\n' "$res2" | jq -c '[.result.tools[].name]')
if [ "$names1" != "$names2" ]; then
    echo "FALHA MCP-V: ordem de tools/list não determinística ($names1 != $names2)" >&2
    exit 1
fi

# Ferramenta sem parâmetros tem {"type": "object", "additionalProperties": false}
printf '%s\n' "$res1" | jq -e '.result.tools[] | select(.name == "no_args_cmd") | .inputSchema == {"type": "object", "additionalProperties": false}' >/dev/null
# Ferramenta com parâmetros tem properties.args
printf '%s\n' "$res1" | jq -e '.result.tools[] | select(.name == "echo_test") | .inputSchema.properties.args.type == "string"' >/dev/null
# Resultado moderno tem resultType, ttlMs e cacheScope
printf '%s\n' "$res1" | jq -e '.result.resultType == "complete"' >/dev/null
printf '%s\n' "$res1" | jq -e '.result.ttlMs == 300000' >/dev/null
printf '%s\n' "$res1" | jq -e '.result.cacheScope == "public"' >/dev/null
echo "  [OK] tools/list ordem determinística, schemas e campos de cache validados"

# 2.5 tools/call com sucesso: não inclui 'Executing: ...' e isError: false
req='{"jsonrpc": "2.0", "id": 6, "method": "tools/call", "params": {"name": "echo_test", "arguments": {"args": "HelloMCP"}}}'
res=$(run_mcp_session "$req")
printf '%s\n' "$res" | jq -e '.result.isError == false' >/dev/null
printf '%s\n' "$res" | jq -e '.result.content[0].type == "text"' >/dev/null
printf '%s\n' "$res" | jq -e '.result.content[0].text | contains("ECHO:HelloMCP")' >/dev/null
if printf '%s\n' "$res" | jq -r '.result.content[0].text' | grep -q "Executing:"; then
    echo "FALHA MCP-1: 'Executing:' vazou para o resultado da ferramenta MCP" >&2
    exit 1
fi
echo "  [OK] tools/call sucesso: isError=false e sem prefixo 'Executing:'"

# 2.6 tools/call com falha (exit 3): responde isError: true e inclui status no texto
req='{"jsonrpc": "2.0", "id": 7, "method": "tools/call", "params": {"name": "fail_exit3"}}'
res=$(run_mcp_session "$req")
printf '%s\n' "$res" | jq -e '.result.isError == true' >/dev/null
printf '%s\n' "$res" | jq -e '.result.content[0].text | contains("3")' >/dev/null
echo "  [OK] tools/call falha: isError=true e status 3 no texto"

# 2.7 tools/call com comando que lê stdin: recebe EOF e não trava nem consome JSON-RPC
req='{"jsonrpc": "2.0", "id": 8, "method": "tools/call", "params": {"name": "stdin_reader"}}'
res=$(run_mcp_session "$req")
printf '%s\n' "$res" | jq -e '.result.isError == false' >/dev/null
printf '%s\n' "$res" | jq -e '.result.content[0].text | contains("GOT_EOF")' >/dev/null
echo "  [OK] tools/call stdin_reader recebeu EOF direto (/dev/null)"

# 2.8 tools/call com timeout_s: 2 e sleep 30: termina em menos de 4s com isError: true
t_start=$(date +%s)
req='{"jsonrpc": "2.0", "id": 9, "method": "tools/call", "params": {"name": "timeout_cmd"}}'
res=$(run_mcp_session "$req")
t_end=$(date +%s)
t_elapsed=$((t_end - t_start))
if [ "$t_elapsed" -ge 4 ]; then
    echo "FALHA MCP-2: tools/call timeout_cmd demorou ${t_elapsed}s, esperado < 4s" >&2
    exit 1
fi
printf '%s\n' "$res" | jq -e '.result.isError == true' >/dev/null
printf '%s\n' "$res" | jq -e '.result.content[0].text | contains("timed out") or contains("expirou")' >/dev/null
echo "  [OK] tools/call timeout_cmd finalizou em ${t_elapsed}s com isError=true"

# 2.9 tools/call com max_output_bytes: saída é truncada com aviso
req='{"jsonrpc": "2.0", "id": 10, "method": "tools/call", "params": {"name": "trunc_cmd"}}'
res=$(run_mcp_session "$req")
printf '%s\n' "$res" | jq -e '.result.content[0].text | contains("truncated") or contains("truncada")' >/dev/null
echo "  [OK] tools/call trunc_cmd truncou saída com aviso"

# 2.10 tools/call ferramenta inexistente retorna erro -32602
req='{"jsonrpc": "2.0", "id": 11, "method": "tools/call", "params": {"name": "ferramenta_fantasma"}}'
res=$(run_mcp_session "$req")
printf '%s\n' "$res" | jq -e '.error.code == -32602' >/dev/null
printf '%s\n' "$res" | jq -e '.error.message | contains("Unknown tool")' >/dev/null
echo "  [OK] Ferramenta inexistente retorna erro -32602"

# 2.11 Método inexistente retorna erro -32601
req='{"jsonrpc": "2.0", "id": 12, "method": "metodo/fantasma"}'
res=$(run_mcp_session "$req")
printf '%s\n' "$res" | jq -e '.error.code == -32601' >/dev/null
printf '%s\n' "$res" | jq -e '.error.message | contains("Method not found")' >/dev/null
echo "  [OK] Método inexistente retorna erro -32601"

# 2.12 Linha que não é JSON retorna Parse Error (-32700)
res=$(printf 'esta linha nao eh um json valido\n' | "$BIN" -C "$CONFIG_FILE" --mcp)
printf '%s\n' "$res" | jq -e '.error.code == -32700' >/dev/null
printf '%s\n' "$res" | jq -e '.error.message == "Parse error"' >/dev/null
echo "  [OK] Linha malformada retorna Parse error (-32700)"

# 2.13 MCP-3 e MCP-4: tools/list reflete inputSchema tipado e annotations
req='{"jsonrpc": "2.0", "id": 40, "method": "tools/list"}'
res=$(run_mcp_session "$req")
# Validação do inputSchema tipado para typed_cmd
printf '%s\n' "$res" | jq -e '.result.tools[] | select(.name == "typed_cmd") | .inputSchema.properties.mode.type == "string"' >/dev/null
printf '%s\n' "$res" | jq -e '.result.tools[] | select(.name == "typed_cmd") | .inputSchema.properties.mode.enum == ["fast", "safe"]' >/dev/null
printf '%s\n' "$res" | jq -e '.result.tools[] | select(.name == "typed_cmd") | .inputSchema.properties.count.type == "integer"' >/dev/null
printf '%s\n' "$res" | jq -e '.result.tools[] | select(.name == "typed_cmd") | .inputSchema.properties.count.minimum == 1' >/dev/null
printf '%s\n' "$res" | jq -e '.result.tools[] | select(.name == "typed_cmd") | .inputSchema.properties.count.maximum == 10' >/dev/null
printf '%s\n' "$res" | jq -e '.result.tools[] | select(.name == "typed_cmd") | .inputSchema.required == ["mode", "count"]' >/dev/null
printf '%s\n' "$res" | jq -e '.result.tools[] | select(.name == "typed_cmd") | .inputSchema.additionalProperties == false' >/dev/null
# Validação de annotations para typed_cmd e destructive_cmd
printf '%s\n' "$res" | jq -e '.result.tools[] | select(.name == "typed_cmd") | .annotations.readOnlyHint == true' >/dev/null
printf '%s\n' "$res" | jq -e '.result.tools[] | select(.name == "typed_cmd") | .annotations.destructiveHint == false' >/dev/null
printf '%s\n' "$res" | jq -e '.result.tools[] | select(.name == "typed_cmd") | .annotations.idempotentHint == true' >/dev/null
printf '%s\n' "$res" | jq -e '.result.tools[] | select(.name == "typed_cmd") | .annotations.openWorldHint == false' >/dev/null
printf '%s\n' "$res" | jq -e '.result.tools[] | select(.name == "destructive_cmd") | .annotations.destructiveHint == true' >/dev/null
printf '%s\n' "$res" | jq -e '.result.tools[] | select(.name == "destructive_cmd") | .annotations.readOnlyHint == false' >/dev/null
echo "  [OK] tools/list inputSchema tipado e annotations validados"

# 2.14 MCP-4: admin_only filtrado para usuário comum e visível para admin
res_guest=$(printf '{"jsonrpc": "2.0", "id": 41, "method": "tools/list"}\n' | AETHERCLI_USER=operador "$BIN" -C "$CONFIG_FILE" --mcp)
if printf '%s\n' "$res_guest" | jq -e '.result.tools | any(.name == "admin_cmd")' >/dev/null; then
    echo "FALHA MCP-4: admin_cmd apareceu na lista de ferramentas para usuario operador!" >&2
    exit 1
fi
res_guest_call=$(printf '{"jsonrpc": "2.0", "id": 42, "method": "tools/call", "params": {"name": "admin_cmd"}}\n' | AETHERCLI_USER=operador "$BIN" -C "$CONFIG_FILE" --mcp)
printf '%s\n' "$res_guest_call" | jq -e '.error.code == -32602' >/dev/null
# A árvore de administração nunca é servida, nem para o usuário admin.
res_admin=$(printf '{"jsonrpc": "2.0", "id": 43, "method": "tools/list"}\n' | AETHERCLI_USER=admin "$BIN" -C "$CONFIG_FILE" --mcp)
printf '%s\n' "$res_admin" | jq -e '[.result.tools[].name] | index("admin_cmd") == null' >/dev/null
res_admin_call=$(printf '{"jsonrpc": "2.0", "id": 44, "method": "tools/call", "params": {"name": "admin_cmd"}}\n' | AETHERCLI_USER=admin "$BIN" -C "$CONFIG_FILE" --mcp)
printf '%s\n' "$res_admin_call" | jq -e '.error.code == -32602' >/dev/null
printf '%s\n' "$res_admin_call" | jq -e '.result == null' >/dev/null
echo "  [OK] MCP-4 controle de acesso admin_only em tools/list e tools/call validado"

# 2.15 MCP-3: tools/call com parâmetros tipados válidos
req='{"jsonrpc": "2.0", "id": 45, "method": "tools/call", "params": {"name": "typed_cmd", "arguments": {"mode": "fast", "count": 7, "flag": true}}}'
res=$(run_mcp_session "$req")
printf '%s\n' "$res" | jq -e '.result.isError == false' >/dev/null
printf '%s\n' "$res" | jq -e '.result.content[0].text | contains("ARG1=fast;ENV_MODE=fast;ENV_COUNT=7;ENV_FLAG=true")' >/dev/null
echo "  [OK] tools/call repassa parâmetros tipados e env vars com sucesso"

# 2.16 MCP-3: tools/call com enum inválido rejeitado com isError: true
req='{"jsonrpc": "2.0", "id": 46, "method": "tools/call", "params": {"name": "typed_cmd", "arguments": {"mode": "superfast", "count": 5}}}'
res=$(run_mcp_session "$req")
printf '%s\n' "$res" | jq -e '.result.isError == true' >/dev/null
printf '%s\n' "$res" | jq -e '.result.content[0].text | contains("must be one of") or contains("deve ser um de")' >/dev/null
echo "  [OK] tools/call validação de enum rejeita com isError=true"

# 2.17 MCP-3: tools/call com integer fora de faixa rejeitado com isError: true
req='{"jsonrpc": "2.0", "id": 47, "method": "tools/call", "params": {"name": "typed_cmd", "arguments": {"mode": "safe", "count": 99}}}'
res=$(run_mcp_session "$req")
printf '%s\n' "$res" | jq -e '.result.isError == true' >/dev/null
printf '%s\n' "$res" | jq -e '.result.content[0].text | contains("out of range") or contains("fora da faixa")' >/dev/null
echo "  [OK] tools/call validação de faixa numérica rejeita com isError=true"

# 2.18 MCP-3: tools/call faltando parâmetro obrigatório rejeitado com isError: true
req='{"jsonrpc": "2.0", "id": 48, "method": "tools/call", "params": {"name": "typed_cmd", "arguments": {"mode": "safe"}}}'
res=$(run_mcp_session "$req")
printf '%s\n' "$res" | jq -e '.result.isError == true' >/dev/null
printf '%s\n' "$res" | jq -e '.result.content[0].text | contains("required") or contains("obrigat")' >/dev/null
echo "  [OK] tools/call validação de parâmetro obrigatório rejeita com isError=true"


echo "=== Testando regressões de robustez e segurança ==="

REG_CONFIG="$TMP_DIR/reg_config.json"
cat << 'CFG' > "$REG_CONFIG"
{
  "commands": [
    { "name": "errout", "activation": "echo OUT_TXT; echo ERR_TXT >&2" },
    { "name": "bigerr", "activation": "head -c 200000 /dev/zero | tr '\\0' x >&2; echo DONE" },
    { "name": "binary", "activation": "printf '\\377\\376x'" },
    { "name": "utf8cut", "activation": "printf 'ééééé'", "max_output_bytes": 3 },
    { "name": "flagged", "activation": "printf 'ARG=%s\\n'", "params": [{ "name": "v", "type": "string" }] },
    { "name": "flagged_eoo", "activation": "printf 'ARG=%s\\n' --", "params": [{ "name": "v", "type": "string" }] },
    { "name": "shifted", "activation": "printf '1=[%s] 2=[%s]\\n'", "params": [{ "name": "a", "type": "string" }, { "name": "b", "type": "string" }] },
    { "name": "reload", "activation": "internal:reload" },
    { "name": "notified", "activation": "touch notified.mark", "cwd": "marks" },
    { "name": "closefd", "activation": "exec >&- 2>&-; sleep 15", "timeout_s": 1 },
    { "name": "survivor", "activation": "(trap '' TERM; sleep 47) & sleep 47", "timeout_s": 1 },
    { "name": "num", "activation": "echo N=$AETHERCLI_PARAM_N", "params": [{ "name": "n", "type": "integer", "maximum": 10 }] },
    { "name": "stdin_wait", "activation": "read -r line; echo \"GOT:$line\"" }
  ]
}
CFG
mkdir -p "$TMP_DIR/marks"
PING='{"jsonrpc":"2.0","id":99,"method":"tools/list"}'
reg_call() {
    args=${2:-}
    [ -n "$args" ] || args='{}'
    printf '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"%s","arguments":%s}}\n' "$1" "$args"
}
reg_session() {
    printf '%s\n' "$@" | "$BIN" -C "$REG_CONFIG" --mcp
}
reg_alive() {
    printf '%s\n' "$1" | sed -n '$p' | jq -e '.id == 99' >/dev/null
}

# 3.1 stderr é capturado junto com stdout
res=$(reg_session "$(reg_call errout)")
printf '%s\n' "$res" | jq -e '.result.content[0].text | contains("OUT_TXT") and contains("ERR_TXT")' >/dev/null
echo "  [OK] stdout e stderr capturados"

# 3.2 stderr acima do buffer do pipe não trava o servidor
res=$(reg_session "$(reg_call bigerr)")
printf '%s\n' "$res" | jq -e '.result.content[0].text | contains("DONE")' >/dev/null
echo "  [OK] stderr de 200 KB drenado sem bloquear"

# 3.3 saída não UTF-8 e truncamento no meio de um caractere mantêm o servidor vivo
res=$(reg_session "$(reg_call binary)" "$(reg_call utf8cut)" "$PING")
reg_alive "$res"
echo "  [OK] saída não UTF-8 não derruba o servidor"

# 3.4 tipos inválidos no JSON-RPC não derrubam o servidor
res=$(reg_session '{"jsonrpc":"2.0","id":1,"method":1}' '{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":1}}' '{"jsonrpc":"2.0","id":3,"method":"tools/call","params":[]}' "$PING")
printf '%s\n' "$res" | sed -n '1p' | jq -e '.error.code == -32600' >/dev/null
printf '%s\n' "$res" | sed -n '2p' | jq -e '.error.code == -32602' >/dev/null
printf '%s\n' "$res" | sed -n '3p' | jq -e '.error.code == -32602' >/dev/null
reg_alive "$res"
echo "  [OK] tipos inválidos respondem erro e o servidor continua"

# 3.5 valor iniciado por '-' em parâmetro string exige ativação terminada em ' --'
res=$(reg_session "$(reg_call flagged '{"v":"--evil"}')")
printf '%s\n' "$res" | jq -e '.result.isError == true' >/dev/null
res=$(reg_session "$(reg_call flagged_eoo '{"v":"--ok"}')")
printf '%s\n' "$res" | jq -e '.result.content[0].text | contains("ARG=--ok")' >/dev/null
set +e
output=$("$BIN" -C "$REG_CONFIG" -p 'flagged --evil' 2>&1)
status=$?
set -e
[ "$status" -ne 0 ]
assert_absent "$output" 'ARG=--evil'
echo "  [OK] injeção de flag por parâmetro recusada (MCP e -p)"

# 3.6 opcional omitido não desloca o valor seguinte
res=$(reg_session "$(reg_call shifted '{"b":"B"}')")
printf '%s\n' "$res" | jq -e '.result.content[0].text | contains("1=[] 2=[B]")' >/dev/null
echo "  [OK] argv posicional preservado com opcional omitido"

# 3.7 ativações internas não viram ferramentas
res=$(reg_session "$PING")
printf '%s\n' "$res" | jq -e '[.result.tools[].name] | index("reload") == null' >/dev/null
echo "  [OK] internal:* fora de tools/list"

# 3.8 notificação não executa nem responde
sed -i.bak "s#\"cwd\": \"marks\"#\"cwd\": \"$TMP_DIR/marks\"#" "$REG_CONFIG"
res=$(reg_session '{"jsonrpc":"2.0","method":"tools/call","params":{"name":"notified"}}')
[ -z "$res" ]
[ ! -e "$TMP_DIR/marks/notified.mark" ]
echo "  [OK] notificação tools/call ignorada"

# 3.9 timeout vale com pipes fechados e mata o grupo inteiro
t_start=$(date +%s)
res=$(reg_session "$(reg_call closefd)")
t_elapsed=$(($(date +%s) - t_start))
[ "$t_elapsed" -lt 5 ]
printf '%s\n' "$res" | jq -e '.result.isError == true' >/dev/null
reg_session "$(reg_call survivor)" >/dev/null
sleep 1
if pgrep -f 'sleep 47' >/dev/null; then
    pkill -f 'sleep 47' || true
    echo "FALHA: processo sobreviveu ao timeout" >&2
    exit 1
fi
echo "  [OK] timeout com pipes fechados e sem sobreviventes"

# 3.10 inteiro acima de LLONG_MAX é recusado, não convertido para negativo
res=$(reg_session "$(reg_call num '{"n":18446744073709551615}')")
printf '%s\n' "$res" | jq -e '.result.isError == true' >/dev/null
echo "  [OK] inteiro sem sinal fora da faixa recusado"

# 3.10b inteiro negativo também é valor iniciado por '-'
res=$(reg_session "$(reg_call num '{"n":-5}')")
printf '%s\n' "$res" | jq -e '.result.isError == true' >/dev/null
echo "  [OK] inteiro negativo recusado sem ativação terminada em ' --'"

# 3.10c o filho não consome o protocolo: a mensagem chega depois de ele iniciar
res=$( { reg_call stdin_wait; sleep 1; printf '%s\n' "$PING"; } | "$BIN" -C "$REG_CONFIG" --mcp)
printf '%s\n' "$res" | sed -n '1p' | jq -e '.result.content[0].text == "GOT:\n"' >/dev/null
reg_alive "$res"
echo "  [OK] stdin do filho isolado do JSON-RPC"

# 3.10d aninhamento extremo é recusado sem derrubar o servidor
deep=$(awk 'BEGIN { for (i = 0; i < 300000; i++) printf "["; }')
res=$(reg_session "{\"jsonrpc\":\"2.0\",\"id\":$deep" "$PING")
printf '%s\n' "$res" | sed -n '1p' | jq -e '.error.code == -32600' >/dev/null
reg_alive "$res"
echo "  [OK] JSON profundo recusado"

# 3.10e id inválido e ping
res=$(reg_session '{"jsonrpc":"2.0","id":{"x":1},"method":"tools/call","params":{"name":"notified"}}' '{"jsonrpc":"2.0","id":5,"method":"ping"}')
printf '%s\n' "$res" | sed -n '1p' | jq -e '.error.code == -32600' >/dev/null
printf '%s\n' "$res" | sed -n '2p' | jq -e '.result.resultType == "complete"' >/dev/null
[ ! -e "$TMP_DIR/marks/notified.mark" ]
echo "  [OK] id inválido recusado e ping respondido"

# 3.10f nomes de ferramenta duplicados impedem a partida
DUP_CONFIG="$TMP_DIR/dup_config.json"
printf '%s\n' '{"commands":[{"name":"a_b","activation":"true"},{"name":"a","subcommands":[{"name":"b","activation":"true"}]}]}' > "$DUP_CONFIG"
set +e
printf '' | "$BIN" -C "$DUP_CONFIG" --mcp >/dev/null 2>&1
status=$?
set -e
[ "$status" -eq 1 ]
echo "  [OK] colisão de nomes recusada na partida"

# 3.10g SIGTERM no servidor não deixa o comando em execução órfão
SLOW_CONFIG="$TMP_DIR/slow_config.json"
printf '%s\n' '{"commands":[{"name":"slow","activation":"sleep 53"}]}' > "$SLOW_CONFIG"
{ printf '%s\n' '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"slow"}}'; sleep 3; } | "$BIN" -C "$SLOW_CONFIG" --mcp >/dev/null 2>&1 &
sleep 1
pkill -TERM -f "$SLOW_CONFIG" || true
sleep 1
if pgrep -f 'sleep 53' >/dev/null; then
    pkill -f 'sleep 53' || true
    echo "FALHA: comando sobreviveu ao SIGTERM do servidor" >&2
    exit 1
fi
wait || true
echo "  [OK] SIGTERM encaminhado ao comando"

# 3.10h SIGTERM não espera indefinidamente um comando que o ignora
STUBBORN_CONFIG="$TMP_DIR/stubborn_config.json"
printf '%s\n' '{"commands":[{"name":"stubborn","activation":"trap \"\" TERM HUP; sleep 59"}]}' > "$STUBBORN_CONFIG"
{ printf '%s\n' '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"stubborn"}}'; sleep 6; } | "$BIN" -C "$STUBBORN_CONFIG" --mcp >/dev/null 2>&1 &
sleep 1
pkill -TERM -f "$STUBBORN_CONFIG" || true
sleep 3
still=0
pgrep -f "$STUBBORN_CONFIG" >/dev/null && still=1
pgrep -f 'sleep 59' >/dev/null && still=1
pkill -KILL -f "$STUBBORN_CONFIG" || true
pkill -KILL -f 'sleep 59' || true
wait || true
[ "$still" -eq 0 ]
echo "  [OK] SIGTERM encerra servidor e comando teimoso no prazo de graça"

# 3.10i sinal ignorado na herança (nohup) continua valendo para o comando
NOHUP_CONFIG="$TMP_DIR/nohup_config.json"
printf '%s\n' '{"commands":[{"name":"slp","activation":"sleep 2; echo SLP_DONE"}]}' > "$NOHUP_CONFIG"
output=$(sh -c "trap '' HUP; exec \"$BIN\" -C \"$NOHUP_CONFIG\" -p slp" & p=$!; sleep 1; kill -HUP "$p"; wait "$p")
printf '%s\n' "$output" | grep -q SLP_DONE
res=$( { printf '%s\n' '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"slp"}}'; sleep 1; pkill -HUP -f "$NOHUP_CONFIG" || true; sleep 2; } | sh -c "trap '' HUP; exec \"$BIN\" -C \"$NOHUP_CONFIG\" --mcp")
printf '%s\n' "$res" | jq -e '.result.isError == false and (.result.content[0].text | contains("SLP_DONE"))' >/dev/null
echo "  [OK] SIGHUP ignorado na herança (nohup) respeitado"

# 3.10j subárvore configure e descendentes de admin_only ficam fora, sem depender do usuário
ADMIN_TREE_CONFIG="$TMP_DIR/admin_tree.json"
cat << 'CFG' > "$ADMIN_TREE_CONFIG"
{ "commands": [
    { "name": "configure", "subcommands": [
        { "name": "terminal", "activation": "internal:config_mode", "subcommands": [
            { "name": "whoami", "activation": "echo CONFIGURE_RAN" }
        ] }
    ] },
    { "name": "ops", "admin_only": true, "subcommands": [
        { "name": "child", "activation": "echo CHILD_RAN" }
    ] },
    { "name": "app", "subcommands": [
        { "name": "configure", "activation": "echo APP_CONFIGURE_OK" }
    ] }
] }
CFG
res=$(printf '{"jsonrpc":"2.0","id":1,"method":"tools/list"}\n' | AETHERCLI_USER=admin "$BIN" -C "$ADMIN_TREE_CONFIG" --mcp)
printf '%s\n' "$res" | jq -e '[.result.tools[].name] == ["app_configure"]' >/dev/null
for tool in configure_terminal_whoami ops_child; do
    res=$(printf '{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"%s"}}\n' "$tool" | AETHERCLI_USER=admin "$BIN" -C "$ADMIN_TREE_CONFIG" --mcp)
    printf '%s\n' "$res" | jq -e '.error.code == -32602' >/dev/null
done
# A árvore padrão não expõe nenhuma ferramenta de configure.
DEFAULT_CONFIG="$SCRIPT_DIR/etc/config.json"
res=$(printf '{"jsonrpc":"2.0","id":3,"method":"tools/list"}\n' | "$BIN" -C "$DEFAULT_CONFIG" --mcp)
printf '%s\n' "$res" | jq -e '([.result.tools[].name | select(startswith("configure"))] | length) == 0 and (.result.tools | length) > 0' >/dev/null
echo "  [OK] árvore de administração fora do MCP"

# 3.11 string com NUL é recusada
res=$(reg_session "$(reg_call flagged '{"v":"a\u0000b"}')")
printf '%s\n' "$res" | jq -e '.result.isError == true' >/dev/null
echo "  [OK] NUL em parâmetro recusado"

echo "=== Testando autenticação do servidor MCP ==="
AUTH_CONFIG="$TMP_DIR/auth_config.json"
cat << 'CFG' > "$AUTH_CONFIG"
{ "require_authentication": true, "commands": [
    { "name": "plain", "activation": "echo PLAIN_RAN; echo \"PW=${AETHERCLI_PASSWORD:-}\"" },
    { "name": "danger", "activation": "echo DANGER_RAN", "admin_only": true }
] }
CFG
auth_session() {
    printf '%s\n' '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"'"$1"'"}}' | "$BIN" -C "$AUTH_CONFIG" --mcp 2>/dev/null
}

# 4.0 sem arquivo de usuários não há bootstrap de admin no servidor
set +e
out=$(printf '%s\n' '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"danger"}}' | "$BIN" -C "$AUTH_CONFIG" --mcp 2>/dev/null)
status=$?
set -e
[ "$status" -eq 1 ]
[ -z "$out" ]
echo "  [OK] MCP sem users.json falha fechado"

printf 'senha12345\nsenha12345\n' | "$BIN" -C "$AUTH_CONFIG" --adduser operador >/dev/null

# 4.1 sem credencial, com identidade só alegada ou senha errada: não sobe
set +e
out=$(auth_session plain); s1=$?
out2=$(AETHERCLI_USER=admin auth_session danger); s2=$?
out3=$(AETHERCLI_USER=operador AETHERCLI_PASSWORD=errada auth_session plain); s3=$?
set -e
[ "$s1" -eq 1 ]
[ "$s2" -eq 1 ]
[ "$s3" -eq 1 ]
[ -z "$out$out2$out3" ]
echo "  [OK] MCP com autenticação exigida falha fechado"

# 4.2 credencial válida autentica, respeita admin_only e a senha não chega ao filho
res=$(AETHERCLI_USER=operador AETHERCLI_PASSWORD=senha12345 auth_session plain)
printf '%s\n' "$res" | jq -e '.result.content[0].text | contains("PLAIN_RAN") and contains("PW=\n")' >/dev/null
res=$(AETHERCLI_USER=operador AETHERCLI_PASSWORD=senha12345 auth_session danger)
printf '%s\n' "$res" | jq -e '.error.code == -32602' >/dev/null
echo "  [OK] credencial válida autentica sem vazar a senha"

echo "=== Todos os testes de conformidade MCP e -p passaram com sucesso! ==="
