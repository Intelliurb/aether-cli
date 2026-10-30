/*
 * Copyright (c) 2026 caq@intelliurb.com
 */
#pragma once
#include <string>
#include <vector>

#include "CommandParser.hpp"

namespace cli::completer {

using cli::parser::Command;

struct Resolution {
    const Command* cmd = nullptr;   // comando mais profundo resolvido
    size_t depth = 0;               // quantos tokens foram consumidos como comando
    bool ambiguous = false;         // a resolução parou por abreviação ambígua
    std::string ambiguous_token;
    bool requires_admin = false;    // admin_only em qualquer nível do caminho
};

// Match exato tem prioridade; senão, abreviação por prefixo único (estilo
// Cisco: "sh ip ro" == "show ip route").
inline const Command* matchToken(const std::vector<Command>& level, const std::string& token, bool& ambiguous) {
    ambiguous = false;
    const Command* prefix_match = nullptr;
    int prefix_count = 0;
    for (const auto& cmd : level) {
        if (cmd.name == token || (!cmd.abbrev.empty() && cmd.abbrev == token)) return &cmd;
        if (cmd.name.compare(0, token.size(), token) == 0) {
            prefix_match = &cmd;
            ++prefix_count;
        }
    }
    if (prefix_count == 1) return prefix_match;
    if (prefix_count > 1) ambiguous = true;
    return nullptr;
}

// Resolve o máximo de tokens possível na árvore. Tokens além de `depth` são
// argumentos do comando (ou entrada inválida, a critério do chamador).
inline Resolution resolve(const std::vector<Command>& tree, const std::vector<std::string>& tokens) {
    Resolution r;
    const std::vector<Command>* level = &tree;
    for (const auto& t : tokens) {
        bool amb = false;
        const Command* m = matchToken(*level, t, amb);
        if (!m) {
            if (amb) {
                r.ambiguous = true;
                r.ambiguous_token = t;
            }
            break;
        }
        r.cmd = m;
        r.requires_admin = r.requires_admin || m->admin_only;
        level = &m->subcommands;
        ++r.depth;
    }
    return r;
}

inline std::vector<const Command*> prefixMatches(const std::vector<Command>& level, const std::string& prefix) {
    std::vector<const Command*> out;
    for (const auto& c : level) {
        if (c.name.compare(0, prefix.size(), prefix) == 0) out.push_back(&c);
    }
    return out;
}

inline std::string longestCommonPrefix(const std::vector<const Command*>& cmds) {
    if (cmds.empty()) return "";
    std::string lcp = cmds[0]->name;
    for (size_t i = 1; i < cmds.size(); ++i) {
        const std::string& n = cmds[i]->name;
        size_t j = 0;
        while (j < lcp.size() && j < n.size() && lcp[j] == n[j]) ++j;
        lcp.resize(j);
    }
    return lcp;
}

inline std::vector<std::string> prefixMatchStrings(const std::vector<std::string>& candidates, const std::string& prefix) {
    std::vector<std::string> out;
    for (const auto& s : candidates) {
        if (s.compare(0, prefix.size(), prefix) == 0) out.push_back(s);
    }
    return out;
}

inline std::string longestCommonPrefixStrings(const std::vector<std::string>& candidates) {
    if (candidates.empty()) return "";
    std::string lcp = candidates[0];
    for (size_t i = 1; i < candidates.size(); ++i) {
        const std::string& n = candidates[i];
        size_t j = 0;
        while (j < lcp.size() && j < n.size() && lcp[j] == n[j]) ++j;
        lcp.resize(j);
    }
    return lcp;
}

} // namespace cli::completer
