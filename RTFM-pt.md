# aethercli - Manual de Instruções (RTFM)

O **aethercli** é uma interface de linha de comando avançada, inspirada no Cisco IOS, projetada para automação de tarefas e gerenciamento de servidores com macros.

---

## 1. Instalação e Compilação

### Pré-requisitos
- Compilador C++17 (GCC 9+, Clang, ou MSVC)
- CMake 3.14+
- Conexão com a internet (para baixar dependências automaticamente)

### Compilando
```bash
mkdir build
cd build
cmake -DCMAKE_INSTALL_PREFIX=/usr/local ..
make -j$(nproc)
sudo make install
```

### Locais de Instalação
Com o prefixo acima, o `make install` posiciona os arquivos em:
- Binário: `/usr/local/bin/aethercli`
- Configurações: `/usr/local/etc/aethercli/` (`config.json`, arquivos de idioma, macros)
- Scripts: `/usr/local/etc/aethercli/scripts/` (scripts bash internos)
- Páginas de manual (man): `/usr/local/share/man/man1/aethercli.1` (acesse via `man aethercli`)

O caminho padrão de configuração é gravado no binário em tempo de compilação (variável de cache CMake `AETHERCLI_CONFIG`); ele pode ser sobrescrito em tempo de execução usando `-C <file>`. O caminho do diretório `var` (usado para locks de sessões) também pode ser especificado via `-DAETHERCLI_VAR_DIR=/path/to/var` na compilação.

> **Aviso:** a reinstalação sobrescreve a pasta `etc/` no diretório de instalação, substituindo quaisquer edições locais do `config.json`. Faça backup primeiro, ou mantenha a sua configuração customizada em outro diretório e a chame usando `-C`.

### Executando testes
Testes unitários (Catch2) são compilados por padrão:
```bash
cd build && ctest --output-on-failure
```
Desabilite passando `-DAETHERCLI_BUILD_TESTS=OFF`.

---

## 2. Uso Geral

### Modo Interativo
```bash
aethercli [-C config_file] [-l language_code_or_file] [--log [path]]
```
Se o arquivo de configuração não existir, o aethercli imprimirá um aviso e iniciará com um conjunto vazio de comandos (apenas `exit`/`quit` e `?` funcionarão) em vez de abortar. Se o arquivo existir mas for um JSON malformado, isso ainda será um erro fatal.

### Modo Headless (Automação)
```bash
aethercli -p "show version"
```

### Servidor MCP (Model Context Protocol)

O modo `--mcp` expõe a árvore de comandos a agentes de IA por JSON-RPC em stdio, com uma mensagem por linha. Ele é opcional na compilação:

```bash
cmake -DAETHERCLI_ENABLE_MCP=ON ..
aethercli -C /opt/ops/config.json --mcp
```

Para registrar no Claude Code:

```bash
claude mcp add ops -- aethercli -C /opt/ops/config.json --mcp
```

**Protocolo.** Atende às revisões `2026-07-28` (versão por requisição em `params._meta`, `server/discover`) e `2025-11-25` (`initialize`).

**Ferramentas.** Todo comando com `activation` vira uma ferramenta, com o caminho unido por `_` (`database backup` vira `database_backup`):

- `params` gera o esquema de entrada, e valores inválidos são recusados sem executar. Sem `params`, um comando com `"args": "append"` recebe o argumento de texto `args`.
- `effect`, `idempotent` e `open_world` viram as anotações `readOnlyHint`, `destructiveHint`, `idempotentHint` e `openWorldHint`.
- Ativações `internal:` não são expostas, e nomes de ferramenta repetidos impedem a partida.

**Execução.** Cada comando recebe `/dev/null` como stdin, e stdout e stderr voltam juntos no resultado. Falha, sinal ou timeout respondem `isError: true`. Sem `max_output_bytes`, a saída de cada fluxo é limitada a 10 MiB. As chamadas são atendidas uma por vez: um comando longo bloqueia as seguintes.

**Autenticação.** Com `require_authentication`, o servidor exige `AETHERCLI_USER` e `AETHERCLI_PASSWORD` válidos no ambiente e termina com status 1 caso contrário, inclusive sem `users.json`. A senha sai do ambiente na partida e não chega aos comandos.

```bash
claude mcp add ops -e AETHERCLI_USER=operador -e AETHERCLI_PASSWORD='...' -- aethercli -C /opt/ops/config.json --mcp
```

O cliente guarda essa senha na própria configuração: use uma conta dedicada, nunca a do `admin`.

**Árvore de administração.** A subárvore `configure` da raiz e todo comando `admin_only`, com seus subcomandos, nunca são servidos, para nenhum usuário, inclusive `admin`. Um script administrativo fora de `configure` precisa de `admin_only`, porque o aethercli não tem como reconhecê-lo sozinho.

**Atenção: o agente acessa todo o resto da árvore, com ou sem autenticação.** O login só decide se o servidor sobe; a lista de ferramentas não depende do usuário. Para limitar um agente, use um config próprio, só com os comandos necessários. Uma ativação que chame outro script administrativo ou o próprio `aethercli -p` também contorna o filtro, e isso é responsabilidade de quem escreve o config. Rode o servidor em uma conta sem privilégios: o agente tem os direitos dela.

### Instalação em Somente Leitura (Acesso Global)
Para uso seguro e compartilhado por múltiplos usuários no sistema operacional, instale o binário em `/usr/local/bin` com permissões de somente leitura e execução para usuários comuns:
```bash
sudo chown root:root /usr/local/bin/aethercli 2>/dev/null || sudo chown root:wheel /usr/local/bin/aethercli
sudo chmod 0755 /usr/local/bin/aethercli
```

### Padrão de Automação: Scripts Wrapper
Em vez de sobrecarregar um `config.json` global com dezenas de utilitários distintos, crie um *script wrapper* para cada programa ou ferramenta gerenciada. O script fica em um diretório do `$PATH` (como `/usr/local/bin/<nome-app>`) e invoca o `aethercli` repassando a flag `-C` com o arquivo de configuração local daquele serviço:

```bash
#!/bin/sh
# /usr/local/bin/backup-cli
set -e
CONF="/opt/backup-system/etc/config.json"
exec /usr/local/bin/aethercli -C "$CONF" "$@"
```

> **Aviso de Segurança:** O arquivo `config.json` e seu diretório pai devem ser de propriedade exclusiva do `root` com permissão `0644` (diretórios `0755`). Quem edita o `config.json` define os comandos que o host executará.

---

## 3. Interface e Navegação

### Comandos de Navegação (Estilo Cisco)
O aethercli usa um sistema de interação imediata:
- **TAB (1x):** Autocompleta o token atual até o prefixo comum mais longo das correspondências; correspondências únicas são completadas inteiramente (acrescentando um espaço). Caso não haja opções, tocará a campainha do terminal.
- **TAB (2x):** Mostra instantaneamente as opções disponíveis para o nível atual.
- **Interrogação (?):** Mostra instantaneamente as opções disponíveis sem necessidade de pressionar ENTER. Dentro de aspas duplas, o `?` é digitado de forma literal.
- **Abreviações:** Qualquer prefixo único (não ambíguo) funciona como o comando inteiro (`sh ip ro` = `show ip route`). Abreviações ambíguas relatarão `% Ambiguous command`.
- **ENTER:** Executa o comando. Sinais e códigos de saída diferentes de zero do comando subjacente são relatados.
- **reload conf:** Recarrega a quente (hot-reload) o arquivo de configuração (`config.json`), a base de usuários e arquivos de idiomas direto na memória, sem necessidade de reiniciar o shell interativo.
- **exit / quit / Ctrl+D (linha vazia):** Sai do modo de configuração ou termina o programa. As configurações do terminal são restauradas na saída, incluindo sinais SIGINT/SIGTERM/SIGHUP.
- **Ctrl+C:** No prompt, limpa a linha atual. Quando um comando subjacente estiver rodando, interrompe apenas aquele comando — o shell em si continua vivo.

### Edição de Linha
- Setas Esquerda/Direita, Home/End, Delete, Backspace (Suporta UTF-8 — caracteres acentuados são manipulados como uma única coluna).
- **Ctrl+A / Ctrl+E:** Início / Fim da linha.
- **Ctrl+U / Ctrl+K:** Deleta até o Início / Fim da linha.
- **Ctrl+W:** Deleta a palavra antes do cursor.
- **Ctrl+L:** Limpa a tela.
- Setas Cima/Baixo: histórico de comandos. O histórico persiste entre sessões através de `~/.aethercli/history` (modo 0600, limitando a 500 entradas).

---

## 4. Comandos Internos Reservados e Macros

### 4.1 Comandos Internos Reservados
O aethercli reserva diretivas `activation` específicas para recursos da engine:
- `internal:config_mode`: Troca o contexto para o modo de configuração (`aethercli(config)#`).
- `internal:run_macro`: Executa um lote de comandos a partir de um arquivo `.macro`.
- `internal:list_macros`: Lista macros disponíveis no diretório de configurações.

### 4.2 Sistema de Macros
Macros são arquivos texto puros terminados com `.macro`. Cada linha é executada como se tivesse sido digitada no prompt.

1. A engine procura arquivos de texto puro, geralmente com extensão `.macro`, armazenados no seu diretório de configurações (ex: `~/.aethercli/`).
2. Você pode rodar o comando `list macros` (ou `configure terminal` -> `list macros`) para ver o que tem disponível lá.
3. Quando você chama `run macro meuarquivo.macro`, a engine abre esse arquivo e "digita" os comandos linha por linha na própria shell, como se fosse o usuário operando o teclado. Ele redireciona essas linhas direto pro loop de execução nativo (`executeCommand`).
4. Existe até uma proteção estrita contra recursão (um limitador `MAX_MACRO_DEPTH`) no código, para impedir que você crie um macro que chame a si próprio infinitamente e trave o AetherCLI num loop.

**Valor real e Exemplo Prático:**
Em ambientes corporativos e de infraestrutura, é muito comum aplicar configurações em lote ("batch"). Ao invés de digitar dezenas de comandos no prompt, você pode criar um arquivo chamado `setup_inicial.macro` com o conteúdo abaixo:

```text
configure terminal
add user john
add user jane
set language portuguese-brasil
set statusbar on
reload conf
exit
```

E então aplicar todos os passos automaticamente rodando:
`aethercli# run macro setup_inicial.macro`

---

## 5. Configuração de Comandos (`etc/config.json`)

A configuração é **hierárquica**. Isso permite roteamentos complexos e organizados no mesmo estilo de roteadores de mercado.

### Metodologia de Configuração: O Paradigma Objeto-Ação
Ao projetar árvores de comandos no `config.json`, adote sempre a premissa de **Objeto-Ação**:
- **Nível 1 (Objeto):** Identifica o recurso alvo gerenciado (ex: `service`, `database`, `network`, `system`, `user`).
- **Nível 2 (Ação):** Identifica a operação executada sobre aquele recurso (ex: `start`, `stop`, `restart`, `status`, `backup`, `list`).
- **Nível 3 / Argumentos:** Parâmetros ou alvos específicos.

Por que usar Objeto-Ação?
- **Descoberta intuitiva:** O operador digita `database ?` e descobre imediatamente todas as ações disponíveis para banco de dados.
- **Reutilização de verbos sem colisão:** Nomes como `status` ou `start` podem ser repetidos sob objetos distintos (`service status` e `database status`).
- **Autocompletar limpo:** Evita a poluição do primeiro nível de opções ao pressionar TAB.

### Estrutura do Objeto Command
- `name`: Nome do comando (usado no terminal). **Obrigatório.**
- `abbrev`: (Opcional) Abreviação explícita do comando.
- `short_desc`: (Opcional) Descrição curta exibida no Duplo-TAB.
- `description`: (Opcional) Descrição longa ou documentação detalhada do comando.
- `syntax`: (Opcional) Texto exibido como ajuda de sintaxe e uso detalhado (Triplo-TAB).
- `activation`: O script OS, utilitário, ou keyword interna que será disparada.
- `admin_only`: (Opcional) Se `true`, restringe a execução exclusivamente ao usuário autenticado `admin`. No MCP, o comando nunca é servido.
- `args`: `"none"` (padrão) ou `"append"`. Com `"append"`, argumentos não previstos na configuração são concatenados (como tokens sanitizados) diretamente após a execução nativa da chave `activation`. Com `"none"`, sobressalentes são negados. Ver Seção 6.
- `timeout_s`: (Opcional) Tempo máximo de execução em segundos. O grupo de processos é terminado se expirado.
- `max_output_bytes`: (Opcional) Tamanho máximo da saída em bytes; saída excedente é truncada com aviso.
- `cwd`: (Opcional) Diretório de trabalho para execução do comando.
- `env`: (Opcional) Objeto JSON definindo variáveis de ambiente passadas ao comando.
- `params`: (Opcional) Parâmetros posicionais tipados (`name`, `type`, `required`, `enum`, `pattern`, `minimum`, `maximum`, `description`); excludente com `"args": "append"`. Veja o caso de uso em `RTFM.md`.
- `effect`, `idempotent`, `open_world`: (Opcionais) Publicados nas anotações das ferramentas MCP.
- `subcommands`: (Opcional) Uma array que suporta recursivamente os mesmos nós hierárquicos acima.

### Exemplo de Configuração
```json
{
  "motd": "Mensagem de boas vindas\n",
  "commands": [
    {
      "name": "show",
      "short_desc": "Comandos de visualização",
      "subcommands": [
        {
          "name": "disk",
          "short_desc": "Uso de disco",
          "activation": "df -h",
          "syntax": "show disk [path]",
          "args": "append"
        }
      ]
    }
  ]
}
```

---

## 6. Segurança e Auditoria

### Modelo de Execução
- O aethercli apenas executa binários que estão explicitamente previstos em `activation`. Metacaracteres embutidos pelo usuário via shell escapes (como `; rm -rf /`) são inteiramente interceptados: sub-tokens caem sempre parseados e envelopados como argumentos literais escapados para o núcleo Bash.
- Por padrão comandos não aceitam **argumentos sobressalentes** (`"args": "none"`). Opte de forma minuciosa setando `"args": "append"`.
- **Prevenção contra injeção de flags:** Se o comando aceitar argumentos, nenhum usuário terá permissão de enviar inputs começando com o literal de traço negativo `-` (ex., `-exec`), **a menos** que a string nativa `activation` declare de forma final explícita um demarcador the encerramento de flags: o literal isolado ` --` (ex., `ls --`).
- **Hard Limit em Inputs:** Todas as entradas processáveis estão limitadas rigidamente ao teto de 4096 bytes para prevenir DoS ou exaustão de buffers.
- A flag `-C` permite o chaveamento de config. O aethercli por si não é uma fronteira absoluta de escalonamento: se o operador tiver permissão de editar seu próprio `config.json`, ele possui o direito pleno de execução sobre qualquer sub-subprocesso ali definido. Se for montar cenários "Restricted-shell", embrulhe a chamada passando via flag `-C` um config hardcoded protegido pertencente exclusivo de `root`.

### Logs de Execução
Por padrão, **o log é completamente inibido** garantindo proteção contra vazeamento de diretivas sensíveis.
- Para habilitá-lo, passe a flag: `--log`.
- Local de saída: `~/.aethercli/log/aethercli.log`.
- **Atenção:** Em instâncias de produção, nunca habilite essa flag se as strings de prompt possuírem rotinas embutidas com senhas puras, chaves RSA ou hashes. Note ainda que o `~/.aethercli/history` gravará comandos passados via terminal na mesma proporção.

### Proteção FHS (Directory Protection)
- `make install` instala a árvore de configuração via permissões declaradas e explícitas: arquivos em `0644`, pastas em `0755`, sendo seu root operator (por padrão) aquele da instância do Sudoers que invocou a instalação. Apenas proprietários podem alterar arquivos sob essa via.

---

## 7. Autenticação e Gerenciamento de Usuários

O aethercli dá suporte completo a autenticação e validação multi-layer para o próprio prompt da interface.

### Como Acionar
Apenas implemente o bloco abaixo em sua raíz `config.json`:
```json
{
  "require_authentication": true,
  "passwd-file": "/etc/aethercli/users.json",
  "number_auth_fail": 3,
  "restricted_session": true
}
```
Caso `passwd-file` seja abstraído ou anulado, o subsistema buscará pelo padrão fixo `users.json` contido exatamente na mesma pasta alocadora de configurações.

### Limitador de Sessões Ativas (Restrições)
Se `restricted_session` contiver a flag em estado ligado `true` (seu default flutua na posição `false`), o processo de escalonamento usará os Locks da POSIX C API criando barreiras de concorrência a níveis do file descriptor numa sub-pasta base `sessions/`.

O path oficial dessa pasta é ditado pela flag de tempo-de-compilação the CMake `AETHERCLI_VAR_DIR`:
```bash
cmake -DAETHERCLI_VAR_DIR=/var/run/aethercli ..
```
Se omitido da compilação global da engine, a diretiva tentará de forma dinâmica construir uma pasta fallback subdiretória de seu binário, baseando na resolução posicional relativa a sua própria configuração JSON. (Exemplo: configurações vivendo em `./etc` trarão a alocação `var/sessions` para viver próxima dali).

### Formato Criptográfico do DB de Usuários
Senhas e credenciais mantidas sob a extensão `.json` (`users.json`):
```json
{ "users": [ { "username": "admin", "salt": "<hex>", "hash": "<hex>", "iterations": 600000 } ] }
```
A fundação de hash é processada de maneira íntegra por **PBKDF2-HMAC-SHA256** batendo a quantia de `600,000` loops/iterações contra 16 bytes puros pseudo-aleatórios designados de *salt*. Os usuários tem de englobar padrões estritos do POSIX (entre 1 até 32 bytes ASCII). Nenhuma senha flutuante inferior a 8 bytes ou superior a 255 é aceita na montagem. Sob hipótese alguma edite esse manifesto `.json` em bloco de notas à mão, e utilize sim seu configurador oficial logo adiante.

### Incluindo Perfis de Instância
Para criação ou atualização limpa dos credenciados, chame as opções nativas de CLI (Veja na Seção 9 a interface integrada) ou faça invocações headless:
```bash
aethercli --adduser admin
```
A variável superposta ambiente (env var) `AETHERCLI_PEPPER` acopla um tempero fixo que é digerido sobre a chave durante a maturação do bloco `PBKDF2`. Ela deve ser fixada identicamente tanto durante execuções headless (--adduser) como durante a carga viva de login das shell's configuradas.

Sem arquivo de usuários, o primeiro login é `admin` com a senha `admin`, e a sessão só abre depois de definir uma nova senha.

Sem essa variável, um `users.json` novo em `~/.aethercli` ganha um pepper aleatório próprio em `~/.aethercli/pepper`, modo `0400`, criado pelo `--adduser` ou pelo primeiro login. Ele nunca é sobrescrito, e apagá-lo invalida as senhas desse arquivo. Um `users.json` que já existia, ou um arquivo fora de `~/.aethercli`, não usa esse pepper. Permissão aberta ou outro dono impedem o login.

O formato está em `etc/users.json.template`, que não contém credenciais e nega o login se copiado como está. `scripts/generate-initial-admin.sh` cria o `admin` com uma senha aleatória exibida uma única vez. Mantenha o `users.json` em modo `0600`, nunca o versione e faça backup dele junto com o `pepper`. O passo a passo está em `RTFM.md`.

---

## 9. Configuração Dinâmica (`configure terminal`)

O aethercli suporta reajuste e modificações modulares do sistema sem que isso fira de forma letal a árvore interativa ou exija recarga do tty. Para chegar as essas ramificações, ative o nó principal entrando no subprompt via a declaração base: `configure terminal`. Note a troca nominal de seu prefixo confirmando o ingresso na base subjacente (`aethercli(config)#`).

### 9.1 Administração de Credenciais
- **`add user <username>`**: Rotina interativa interna que consolida as keys JSON via hash seguro do PBKDF2 e edita perfeitamente seu DB em `users.json`.
- **`rm user <username>`**: Exclusão e destituição pura na árvore de login.
- **`list users`**: Varre os usuários alistáveis dentro de seu ambiente configurado.

### 9.2 Interface e Ajustes Variáveis (Preferences)
- **`set statusbar on|off`**: Modula por perfil as leituras sistêmicas expostas na barra vt100 de terminal do shell e aplica na próxima reentrada em que o arquivo recarregue seu login.
- **`set language english-us|portuguese-brasil`**: Sincroniza em disco e propaga ao bash o token responsável pelo pacote léxico interno do aethercli.

### 9.3 Gráfico de Comandos Dinâmicos
Manipule as raízes e subcomandos do `config.json` sem sair para interfaces externas. É possível encorpar novas diretivas ou arrancar funcionalidades indesejadas direto através da injeção nativa de pathing sob proteção estrita da rotina. Ressalta-se que a base vital de diretivas configuracionais e enraizadas dentro de `/configure` está permanentemente cravada e protegida de podas/mutações indevidas.

- **`add command <parent_path> <name> <short_desc> [activation] [syntax]`**
  Anexa e encorpa a árvore JSON alistando uma rotina inteiramente nova.
  *Sintaxe na prática:* `add command /show "disk" "Show disk usage" "df -h" "show disk"`
  *(Em cenários que objetivam crescer na árvore mestra e base, submeta o alvo `/` vazio na chave de parent path).*

- **`rm command <command_path>`**
  Descola e poda a raiz da diretiva selecionada para a morte.
  *Sintaxe na prática:* `rm command /show/disk`

> **Note:** Ao executar transições modulares na raiz mestra com essas features dinâmicas acimas (seja via *add/rm command* ou via reset no idioma *set language*), consolide e espelhe essas mutações à execução viva sem perder sessão através de seu chamador `reload conf`.

---
*aethercli v0.5.0 - Segurança, Hierarquia, Autenticação, AI e Mutabilidade Dinâmica Config Implemented*
