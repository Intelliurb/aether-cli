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

O MCP nunca serve a árvore de administração: a subárvore `configure` e os comandos
`admin_only`. Com ou sem autenticação, o agente acessa todo o resto, então use um
config próprio para ele, como o deste exemplo. A referência completa do MCP está em
`RTFM-pt.md` e `RTFM-en.md`.

# Usuários e autenticação

Ative o login com `"require_authentication": true` no `config.json`. As contas
ficam no `users.json`, no diretório do `config.json`, ou no caminho indicado por
`"passwd-file"`. O formato está em `etc/users.json.template`, que serve só de
referência: ele não contém credenciais e, copiado como está, nega o login.

## Formato

| Campo | Conteúdo |
|---|---|
| `users[].username` | Começa com `a-z` ou `_`, seguido de `a-z`, `0-9`, `_` ou `-`; até 32 caracteres. `admin` é o único que executa comandos `admin_only` |
| `users[].salt` | 16 bytes aleatórios em hexadecimal, diferentes por usuário |
| `users[].hash` | PBKDF2-HMAC-SHA256 de senha + pepper, 32 bytes em hexadecimal |
| `users[].iterations` | Iterações do PBKDF2; o padrão é 600000 |
| `pepper_file` | `true` quando os hashes dependem de `~/.aethercli/pepper` |

Não escreva `salt` e `hash` à mão. Um arquivo malformado nega o login, e um
`--adduser` sobre ele o substitui, descartando as contas que havia nele.

## Primeira configuração (um único usuário do sistema)

A partir do diretório `etc/` do projeto, ou do diretório instalado:

```sh
mkdir -m 700 ~/.aethercli
cp config.json lang_pt.json lang_en.json ~/.aethercli/
sh scripts/generate-initial-admin.sh ~/.aethercli/config.json
```

O script cria o `admin` com uma senha aleatória de 24 caracteres, que aparece uma
única vez, e recusa rodar se o `users.json` já existir. Se `aethercli` não estiver
no `PATH`, defina `AETHERCLI_BIN`. Ele também cria `~/.aethercli/pepper`.

Até existir um `users.json`, o shell interativo e o `-p` entram como `admin` sem
senha. Crie as contas antes de expor o shell a outras pessoas. O modo MCP não tem
essa exceção.

## Contas seguintes

```sh
aethercli --adduser operador    # cria o usuário ou troca a senha dele
```

No shell, o `admin` usa `add user` e `rm user`; `passwd user` também serve para o
próprio usuário trocar a sua senha. As senhas têm de 8 a 255 caracteres.

## Permissões

| Arquivo | Modo | Observação |
|---|---|---|
| `~/.aethercli/` | `0700` | Diretório de configuração do usuário |
| `users.json` | `0600` | O `--adduser` regrava o arquivo com esse modo |
| `~/.aethercli/pepper` | `0400` | Outro modo ou outro dono nega o login |

## Pepper

- **Por usuário:** um `users.json` novo em `~/.aethercli` ganha um pepper próprio.
- **Instalação compartilhada:** um `users.json` fora de `~/.aethercli` não usa esse
  arquivo, porque todos precisam do mesmo pepper. Use `AETHERCLI_PEPPER`, com o mesmo
  valor no `--adduser` e no login. O valor é herdado pelos comandos executados.
  Quem precisa fazer login precisa ler o `users.json`, então restrinja a leitura a um
  grupo (`chgrp` e `chmod 640`) e refaça esses comandos depois de cada `--adduser`.
- **Backup:** guarde `users.json` e `pepper` juntos, em locais protegidos. Sem o
  pepper, nenhuma senha desse arquivo funciona e não há como recuperá-las: apague os
  dois e recrie as contas.

## Boas práticas

- Nunca versione `users.json` nem `pepper`; o `.gitignore` do projeto já os cobre.
  Salt e hash publicados permitem testar senhas offline.
- Se um `users.json` vazar, troque as senhas de todas as contas dele.
- Troque a senha inicial se ela passou por um local compartilhado.
- O login protege o shell, não a conta do sistema: quem pode escolher outro `-C` usa
  outra árvore de comandos. Para um limite real, instale binário e configuração como
  `root` e entregue o shell por um wrapper ou como shell de login.
- No MCP, passe `AETHERCLI_USER` e `AETHERCLI_PASSWORD` pelo ambiente do cliente; a
  senha é removida do ambiente na partida e não chega aos comandos.

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
