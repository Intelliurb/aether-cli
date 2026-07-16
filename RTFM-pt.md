# aethercli - Manual de Instruções (RTFM)

O **aethercli** é uma interface de linha de comando avançada, inspirada no Cisco IOS, projetada para automação de tarefas e gerenciamento de servidores com suporte nativo para sugestões por IA e macros.

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
- `internal:ai`: Envia os tokens subsequentes como uma requisição de linguagem natural para a IA local (ver Seção 7).

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

### Estrutura do Objeto Command
- `name`: Nome do comando (usado no terminal). **Obrigatório.**
- `short_desc`: Descrição curta exibida no Duplo-TAB.
- `description`: Descrição detalhada (usada via prompt no sistema de IA).
- `syntax`: Texto exibido como ajuda de sintaxe e uso detalhado.
- `activation`: O script OS, utilitário, ou keyword interna que será disparada.
- `args`: `"none"` (padrão) ou `"append"`. Com `"append"`, argumentos não previstos na configuração são concatenados (como tokens sanitizados) diretamente após a execução nativa da chave `activation`. Com `"none"`, sobressalentes são negados. Ver Seção 6.
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

## 7. Integração IA (llama.cpp / Ollama)

O aethercli injeta interativamente o esquema natural para extrair de uma Inteligência Artificial local a melhor e mais correta cadeia de operação. O consumo suporta as bases API no formato nativo da OpenAI (`/v1/chat/completions`), suportando tanto as suítes **llama.cpp** (`llama-server`) quanto os endpoints da ferramenta **Ollama** de maneira 100% agnóstica.

- **Uso:** `ai how do I check the routing table`
- **Fluxo Cíclico:** O sistema joga a árvore iterável do `config.json` para o modelo de base e extrai o `activation` mais pragmático possível que responde de maneira idêntica. Formatações residuais (`markdown`, `think logs`) são drenados e as saídas exigem autorização confirmada por um prompt explícito (`Execute '...'? (y/n)`) antes de seu disparo real. No formato script/headless o `ai` não executará nada, apenas exibirá o log.
- **Chamando via llama.cpp:**
  ```bash
  llama-server -m /opt/llm/Qwen3-VL-2B-Instruct-Q4_K_M.gguf --port 8080
  ```
- **Configurações por Variáveis de Ambiente:**
  - `AI_HOST` (Padrão: `localhost`)
  - `AI_PORT` (Padrão: Auto-probing porta `11434` (Ollama) e então `8080` (llama-server))
  - `AI_MODEL` (Padrão: `default`; O llama-server ignora os pesos da string, o Ollama necessitará do peso com modelo declarado, ex `llama3`)
  - `AI_API_KEY` (Opcional; será processado pelo header nativo `Authorization: Bearer`)
- **Prompt da System Rule:** pode ser sobrescrito localmente pela tradução em json (`lang_*.json`) por meio da key: `ai_system_prompt`.
- Dica: O literal `?` triga a interrogação da árvore nativa no aethercli. Nas requisições contextuais diretas para a rede, exclua ou escape a interrogação para que a query possa ir ilesa ao backend de inference.

---

## 8. Autenticação e Gerenciamento de Usuários

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
