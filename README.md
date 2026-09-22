# aethercli

**aethercli** is a Cisco IOS–style interactive shell for system and application administration. It is not a fixed set of built-in commands: each application (or deployment) supplies its own hierarchical command tree in a `config.json`, mapping phrases such as `show ip route` to activations (shell commands or scripts). Optional macros, localization, authentication, and local AI suggestions build on that tree.

If you start the binary without a configuration file for your application, the command set is empty by design (aside from built-ins such as `exit` / `quit` and `?`). Point `-C` at the application’s config, or install under the default path baked in at build time.

| | |
|---|---|
| Version | 0.6.0 |
| Language | C++17 |
| Build | CMake 3.14+ |
| License | [BSD-3-Clause](LICENSE) |
| Repository | https://github.com/cquintella/aethercli |

## Features

- Hierarchical commands from `config.json` (names, abbreviations, `short_desc`, `syntax`, `activation`)
- Interactive editing: TAB completion, double-TAB / `?` for options, line history under `~/.aethercli/history`
- Context prompts (`aethercli#`, `aethercli(config)#`) and optional status bar
- Headless one-shot execution (`-p`)
- Macros (`.macro` files), optional `require_authentication` with `users.json` (PBKDF2)
- i18n via `lang_en.json` / `lang_pt.json` (and `-l`)

## Build and install

Prerequisites: a C++17 compiler (GCC 9+, Clang, or MSVC), CMake 3.14+, and network access on first configure (nlohmann/json may be fetched if not already installed).

```bash
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local ..
cmake --build . -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"
sudo cmake --install .
```

With that prefix, install places roughly:

- Binary: `/usr/local/bin/aethercli`
- Configuration and scripts: `/usr/local/etc/aethercli/` (`config.json`, `lang_*.json`, `scripts/`, …)

The default config path is compiled in as `AETHERCLI_CONFIG`. Override at runtime with `-C`, or at build time with `-DAETHERCLI_CONFIG=...` / `-DAETHERCLI_CONFIG_DIR=...`.

**Warning:** `cmake --install` / `make install` copies `etc/` into the configuration directory and can overwrite a customized `config.json`. Back it up first, or keep the application config elsewhere and always pass `-C`.

Unit tests (Catch2) build by default:

```bash
cd build && ctest --output-on-failure
```

Disable with `-DAETHERCLI_BUILD_TESTS=OFF`.

## Quick start

```bash
# Interactive shell using the default installed config
aethercli

# Application-specific tree (recommended for custom deployments)
aethercli -C /path/to/your-app/config.json

# Portuguese messages
aethercli -l pt -C /path/to/your-app/config.json

# One command, non-interactive
aethercli -C /path/to/your-app/config.json -p "show system cpu"

aethercli --help
aethercli --version
```

Inside the shell: type `?` or press TAB twice to explore the tree loaded from the active config. A template lives at `config.template.json` / `etc/config.template.json`.

## Documentation

| Document | Content |
|---|---|
| [RTFM-en.md](RTFM-en.md) | Full manual (English): install paths, TAB/`?`, macros, auth, AI, config schema |
| [RTFM-pt.md](RTFM-pt.md) | Manual completo (português) |
| [RTFM.md](RTFM.md) | Macro section (Portuguese); prefer RTFM-en / RTFM-pt for the full guide |
| [AGENTS.md](AGENTS.md) | Notes for automated agents working on this repository |

## License

Copyright (c) 2026 cquintella. Distributed under the terms of the [BSD-3-Clause](LICENSE) license.
