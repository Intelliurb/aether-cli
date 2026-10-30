# aethercli: guia rápido

O manual completo está em `RTFM-pt.md` e `RTFM-en.md`, e nas manpages
(`man aethercli`). Este guia mostra um caso de uso de ponta a ponta e o sistema
de macros.

# Caso de uso: backup com parâmetros validados

Uma equipe quer que operadores e agentes de IA gerem backups sem acesso livre ao
shell. Só podem escolher um banco conhecido e uma retenção de 1 a 90 dias.

## 1. Script

`/opt/ops/scripts/backup.sh`, com permissão de execução:

```sh
#!/bin/sh
# Uso: backup.sh <banco> <retencao_dias> [comprimir]
echo "backup de $1: retenção ${2} dias, comprimir=${3:-false}"
[ "$1" != "falho" ] || { echo "conexão recusada" >&2; exit 3; }
```

Os valores chegam como argumentos posicionais (`$1`, `$2`, `$3`) e também como
`AETHERCLI_PARAM_BANCO`, `AETHERCLI_PARAM_RETENCAO` e
`AETHERCLI_PARAM_COMPRIMIR`. Um parâmetro opcional omitido antes de outro
informado chega como argumento vazio, para não deslocar as posições.

## 2. Árvore de comandos

`/opt/ops/config.json`, no padrão Objeto-Ação:

```json
{
  "language": "pt",
  "scripts_dir": "scripts",
  "commands": [
    {
      "name": "database",
      "short_desc": "Operações de banco de dados",
      "subcommands": [
        {
          "name": "backup",
          "short_desc": "Gerar backup",
          "activation": "${AETHERCLI_SCRIPTS_DIR}/backup.sh",
          "effect": "write",
          "timeout_s": 600,
          "max_output_bytes": 65536,
          "params": [
            { "name": "banco", "type": "string", "required": true, "enum": ["vendas", "estoque", "falho"], "description": "Banco de origem" },
            { "name": "retencao", "type": "integer", "required": true, "minimum": 1, "maximum": 90, "description": "Dias de retenção" },
            { "name": "comprimir", "type": "boolean", "description": "Comprimir o arquivo" }
          ]
        }
      ]
    }
  ]
}
```

Copie `lang_pt.json` para o mesmo diretório para ter as mensagens em português.

## 3. Uso interativo

`aethercli -C /opt/ops/config.json` abre o shell. Depois de `database backup`,
TAB completa os valores do `enum` e `?` descreve o parâmetro da posição atual.

## 4. Automação (cron, CI)

O status de saída é o do comando, e valores inválidos são recusados sem executar
nada:

```text
$ aethercli -C /opt/ops/config.json -p 'database backup vendas 7 true'
Executando: database backup vendas 7 true
backup de vendas: retenção 7 dias, comprimir=true
$ echo $?
0

$ aethercli -C /opt/ops/config.json -p 'database backup vendas 365'
Executando: database backup vendas 365
%% Erro: Parâmetro 'retencao' está fora da faixa permitida.
$ echo $?
1

$ aethercli -C /opt/ops/config.json -p 'database backup falho 7'
Executando: database backup falho 7
backup de falho: retenção 7 dias, comprimir=false
conexão recusada
% Comando terminou com status 3.
$ echo $?
3
```

## 5. Agente de IA (MCP)

Em um binário compilado com `-DAETHERCLI_ENABLE_MCP=ON`, `--mcp` expõe a mesma
árvore por JSON-RPC em stdio. A ferramenta se chama `database_backup`, o esquema
de entrada vem de `params` e `effect` vira as anotações da ferramenta:

```text
$ printf '%s\n' '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"database_backup","arguments":{"banco":"estoque","retencao":30}}}' \
    | aethercli -C /opt/ops/config.json --mcp | jq -c '.result|{isError,text:.content[0].text}'
{"isError":false,"text":"backup de estoque: retenção 30 dias, comprimir=false\n"}

$ printf '%s\n' '{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"database_backup","arguments":{"banco":"vendas","retencao":999}}}' \
    | aethercli -C /opt/ops/config.json --mcp | jq -c '.result|{isError,text:.content[0].text}'
{"isError":true,"text":"%% Erro: Parâmetro 'retencao' está fora da faixa permitida."}
```

Com `"require_authentication": true`, o servidor exige `AETHERCLI_USER` e
`AETHERCLI_PASSWORD` válidos no ambiente e encerra com status 1 caso contrário,
porque o stdin pertence ao protocolo. O bootstrap do login interativo (entrar
como `admin` sem `users.json`) não vale aqui.

# Macros

Uma macro é um arquivo de texto com uma linha de comando do aethercli por
linha. Linhas vazias e linhas cujo primeiro caractere é `#` são ignoradas.

Crie o arquivo no mesmo diretório do `config.json` em uso:

```text
# inventário diário
show system disk
show system memory
show ip interface
```

Salve, por exemplo, como `inventario.macro` e execute:

```text
aethercli# run macro inventario.macro
```

Um caminho relativo é resolvido a partir do diretório do arquivo de
configuração; um caminho absoluto também é aceito:

```text
aethercli# run macro /opt/aethercli/inventario.macro
```

Para listar os arquivos `.macro` disponíveis no diretório da configuração:

```text
aethercli# configure terminal
aethercli(config)# list macros
```

Os comandos são executados na ordem do arquivo. A macro é interrompida no
primeiro comando que falhar, inclusive `exit`. A execução e sua saída são
registradas no diretório da configuração como
`<macro>.<usuário>.<data-hora>.out`. Macros podem chamar outras macros, com
limite de oito níveis de recursão.
