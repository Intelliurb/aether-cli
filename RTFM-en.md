# aethercli - Instructions Manual (RTFM)

**aethercli** is an advanced command-line interface, inspired by Cisco IOS, designed for task automation and server management with built-in support for AI suggestions and macros.

---

## 1. Installation and Compilation

### Prerequisites
- C++17 Compiler (GCC 9+, Clang, or MSVC)
- CMake 3.14+
- Internet connection (to automatically download dependencies)

### Compiling
```bash
mkdir build
cd build
cmake -DCMAKE_INSTALL_PREFIX=/usr/local ..
make -j$(nproc)
sudo make install
```

### Install Locations
With the prefix above, `make install` places:
- Binary: `/usr/local/bin/aethercli`
- Configuration: `/usr/local/etc/aethercli/` (`config.json`, language files, macros)
- Scripts: `/usr/local/etc/aethercli/scripts/` (internal bash scripts)

The default configuration path is baked into the binary at build time (`AETHERCLI_CONFIG` CMake cache variable); override it at runtime with `-C <file>`. You can also configure the default variable files path (used for session locks) via `-DAETHERCLI_VAR_DIR=/path/to/var` at compile time.

> **Warning:** reinstalling copies `etc/` over the installed configuration directory, overwriting any local edits to `config.json`. Back it up first, or keep your customized config elsewhere and load it with `-C`.

### Running the tests
Unit tests (Catch2) build by default:
```bash
cd build && ctest --output-on-failure
```
Disable with `-DAETHERCLI_BUILD_TESTS=OFF`.

---

## 2. General Usage

### Interactive Mode
```bash
aethercli [-C config_file] [-l language_code_or_file] [--log [path]]
```
If the configuration file does not exist, aethercli prints a warning and starts with an empty command set (only `exit`/`quit` and `?` work) instead of aborting. A malformed configuration file is still a fatal error.

### Headless Mode (Automation)
```bash
aethercli -p "show version"
```

---

## 3. Interface and Navigation

### Navigation Commands (Cisco Style)
aethercli uses an immediate interaction system:
- **TAB (1x):** Completes the current token up to the longest common prefix of the matches; a unique match is completed in full (plus a trailing space). No possible completion rings the terminal bell.
- **TAB (2x):** Instantly shows available options for the current level.
- **Question Mark (?):** Instantly shows available options without needing to press ENTER. Inside an open double quote, `?` is inserted literally.
- **Abbreviations:** Any unambiguous prefix works as the full command (`sh ip ro` = `show ip route`). Ambiguous abbreviations are reported (`% Ambiguous command`).
- **ENTER:** Executes the command. Non-zero exit codes and signals of the underlying command are reported.
- **reload conf:** Hot-reloads the configuration file (`config.json`), user database, and language files into memory without restarting the current interactive shell.
- **exit / quit / Ctrl+D (empty line):** Exits configuration mode or terminates the program. Terminal settings are restored on exit, including on SIGINT/SIGTERM/SIGHUP.
- **Ctrl+C:** In the prompt, clears the current line. While a command is running, interrupts only that command — the shell keeps running.

### Line Editing
- Arrows Left/Right, Home/End, Delete, Backspace (UTF-8 aware — accented characters are handled as one column).
- **Ctrl+A / Ctrl+E:** Start / end of line.
- **Ctrl+U / Ctrl+K:** Delete to start / end of line.
- **Ctrl+W:** Delete the word before the cursor.
- **Ctrl+L:** Clear the screen.
- Arrows Up/Down: command history. History persists across sessions in `~/.aethercli/history` (mode 0600, last 500 entries).

---

## 4. Internal Reserved Commands and Macros

### 4.1 Internal Reserved Commands
AetherCLI reserves specific `activation` strings for internal engine features:
- `internal:config_mode`: Switches context to configuration mode (`aethercli(config)#`).
- `internal:run_macro`: Executes a batch of commands from a `.macro` file.
- `internal:list_macros`: Lists available macro files in the configuration directory.
- `internal:ai`: Sends the remaining tokens as a natural-language question to the local AI (see section 7).

### 4.2 Macro System
Macros are simple text files with the `.macro` extension. Each line is executed as a standard CLI command.

1. The engine looks for plain text files, usually with the `.macro` extension, stored in your configuration directory (e.g., `~/.aethercli/`).
2. You can run the command `list macros` (or `configure terminal` -> `list macros`) to see what is available.
3. When you run `run macro my_script.macro`, the engine opens the file and "types" the commands line-by-line into the shell, just as if a user were operating the keyboard. It feeds these lines directly into the native execution loop (`executeCommand`).
4. There is a strict safeguard against recursion (a `MAX_MACRO_DEPTH` limiter) in the source code to prevent a macro from calling itself indefinitely and locking the system in an infinite loop.

**Practical Use Case and Example:**
In enterprise and infrastructure environments, batch configuration is highly common. Instead of typing dozens of commands manually in the prompt, you can create a file named `initial_setup.macro` containing the following:

```text
configure terminal
add user john
add user jane
set language portuguese-brasil
set statusbar on
reload conf
exit
```

And then apply all steps automatically by running:
`aethercli# run macro initial_setup.macro`

---

## 5. Command Configuration (`conf/config.json`)

The configuration is **hierarchical**. This allows for organized subcommands, similar to a professional router.

### Command Object Structure
- `name`: Command name (used in the terminal). **Required.**
- `short_desc`: Short description displayed during Double-TAB.
- `description`: Longer description (used in the AI system prompt).
- `syntax`: Help text displayed for command details.
- `activation`: Operating system command or internal keyword to be executed.
- `args`: `"none"` (default) or `"append"`. With `"append"`, extra user tokens are appended to the `activation` command, each one shell-escaped. With `"none"`, extra tokens are rejected. See section 6.
- `subcommands`: (Optional) A list of other nested commands.

### Configuration Example
```json
{
  "motd": "Welcome message here\n",
  "commands": [
    {
      "name": "show",
      "short_desc": "Visualization commands",
      "subcommands": [
        {
          "name": "disk",
          "short_desc": "Disk usage",
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

## 6. Security and Auditing

### Execution Model
- aethercli only executes what is mapped in the `activation` field. Shell metacharacters in user input (e.g., `; rm -rf /`) are neutralized: extra tokens are passed as quoted, escaped arguments.
- By default a command accepts **no arguments** (`"args": "none"`). Opt in per command with `"args": "append"`.
- **Flag Injection Prevention:** If a command accepts arguments, the user cannot pass arguments starting with `-` (e.g., `-exec`) to prevent flag injection, **unless** the `activation` string explicitly ends with the end-of-options demarcator ` --` (e.g., `ls --`).
- **Input Hard Limit:** All inputs (interactive, headless, or macros) are strictly limited to a maximum of 4096 characters to prevent buffer exhaustion.
- The `-C` flag lets the invoking user load their own configuration. aethercli is **not** a privilege boundary: whoever can write the config file controls what runs. In restricted-shell deployments, invoke aethercli through a wrapper with a fixed `-C` and a root-owned config.

### Execution Logs
By default, **logging is disabled** to protect sensitive data.
- To enable: use the `--log` flag.
- Default location: `~/.aethercli/log/aethercli.log`.
- **Warning:** In production, avoid keeping logs enabled if commands handle passwords or private data. Note that the interactive history (`~/.aethercli/history`) also records typed commands.

### Directory Protection
- `make install` installs the configuration with explicit permissions: files 0644, directories 0755, owned by the installing user (typically root). Only the owner can edit `config.json`.

---

## 7. AI Integration (llama.cpp / Ollama)

aethercli consults a local AI to suggest commands based on natural language. It speaks the OpenAI-compatible API (`/v1/chat/completions`), which is served both by **llama.cpp** (`llama-server`) and by **Ollama** — no configuration flag needed to pick between them.

- **Usage:** `ai how do I check the routing table`
- **Workflow:** the AI receives the list of executable commands and must answer with a single command line. Code fences, backticks, and `<think>` blocks in the reply are stripped. If the suggestion resolves to a real command, aethercli asks for confirmation (`Execute '...'? (y/n)`) before running it. In headless mode suggestions are printed but never executed.
- **Starting a backend with llama.cpp:**
  ```bash
  llama-server -m /opt/llm/Qwen3-VL-2B-Instruct-Q4_K_M.gguf --port 8080
  ```
- **Configuration (environment variables):**
  - `AI_HOST` (default `localhost`)
  - `AI_PORT` (default: probes `11434` (Ollama) then `8080` (llama-server))
  - `AI_MODEL` (default `default`; llama-server ignores it and uses the loaded model, Ollama requires a valid model name, e.g. `llama3`)
  - `AI_API_KEY` (optional; sent as `Authorization: Bearer`)
- **System prompt:** override via the `ai_system_prompt` key in the language file (e.g., `lang_en.json`).
- Tip: `?` triggers inline help at the prompt, so phrase AI questions without it (or quote it).

---

## 8. Authentication and User Management

aethercli supports an optional local authentication system.

### Enabling Authentication
In your JSON configuration file, set:
```json
{
  "require_authentication": true,
  "passwd-file": "/etc/aethercli/users.json",
  "number_auth_fail": 3,
  "restricted_session": true
}
```
If `passwd-file` is omitted, the system defaults to `users.json` in the configuration directory.

### Session Restrictions
If `restricted_session` is set to `true` (defaults to `false`), the system limits a user to a single concurrent session. It enforces this using POSIX file locks placed in a `sessions/` directory.

The location of the session lock files is determined by the `AETHERCLI_VAR_DIR` CMake variable. At compile time, you can configure this location:
```bash
cmake -DAETHERCLI_VAR_DIR=/var/run/aethercli ..
```
If the macro isn't provided or is not found during compilation, the CLI falls back dynamically to a `var/sessions` directory located alongside the configuration directory (e.g., if config is in `./conf`, locks go to `./var/sessions`).

### User Database Format
Users are stored in a JSON file (`users.json`):
```json
{ "users": [ { "username": "admin", "salt": "<hex>", "hash": "<hex>", "iterations": 600000 } ] }
```
Passwords are hashed with PBKDF2-HMAC-SHA256 (600,000 iterations, random 16-byte salt per user). Usernames must adhere to POSIX standards (1-32 characters). Passwords must be between 8 and 255 characters. Do not edit this file by hand; use the tools below.

### Adding Users
To add or update a user securely, use the CLI built-in tools (see Section 9) or the wrapper directly:
```bash
aethercli --adduser admin
```
The optional `AETHERCLI_PEPPER` environment variable adds a secret pepper to the hashes; it must be set to the same value when running aethercli with authentication enabled.

---

## 9. Dynamic Configuration (`configure terminal`)

aethercli supports dynamic configuration and system modifications via its internal command graph. To access these subcommands, you typically start by entering `configure terminal` to get into configuration mode (your prompt will change to `aethercli(config)#`).

### 9.1 User Management
- **`add user <username>`**: Creates or updates a user in the `users.json` authentication database interactively.
- **`rm user <username>`**: Removes a user from the authentication database.
- **`list users`**: Displays all configured users.

### 9.2 UI and Preferences
- **`set statusbar on|off`**: Toggles the system status bar (showing CPU/Mem metrics) for your current user. Takes effect on the next session.
- **`set language english-us|portuguese-brasil`**: Changes the active language of the CLI engine and injected scripts.

### 9.3 Dynamic Command Graph
You can safely mutate `config.json` via the shell, directly expanding or pruning the CLI's capabilities without manually editing the JSON file. Note that the `/configure` tree is restricted and cannot be modified.

- **`add command <parent_path> <name> <short_desc> [activation] [syntax]`**
  Injects a new command into the CLI.
  *Example:* `add command /show "disk" "Show disk usage" "df -h" "show disk"`
  *(If appending to the root, use `/` as `parent_path`)*

- **`rm command <command_path>`**
  Removes an existing command/branch from the CLI.
  *Example:* `rm command /show/disk`

> **Note:** After running any dynamic configuration command (add/rm command, set language), run `reload conf` to hot-swap the internal state in memory so the changes take immediate effect.

---
*aethercli v0.5.0 - Security, Hierarchy, Macros, Authentication, Dynamic Config, and AI Implemented*
