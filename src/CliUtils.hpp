/*
 * Copyright (c) 2026 caq@intelliurb.com
 */
#pragma once
#include <algorithm>
#include <string>
#include <vector>

namespace cli::util {

// Divide a linha em tokens separados por espaço. Aspas duplas agrupam um
// token com espaços e são removidas do resultado.
inline std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> tokens;
    std::string token;
    bool in_quotes = false;
    for (size_t i = 0; i < s.length(); ++i) {
        char c = s[i];
        if (c == '\"') {
            in_quotes = !in_quotes;
        } else if (c == ' ' && !in_quotes) {
            if (!token.empty()) {
                tokens.push_back(token);
                token.clear();
            }
        } else {
            token += c;
        }
    }
    if (!token.empty()) tokens.push_back(token);
    return tokens;
}

// Escapa um argumento para uso dentro de aspas duplas no /bin/sh.
// Dentro de "..." o sh só interpreta: " \ $ `
inline std::string escape_shell_arg(const std::string& arg) {
    std::string escaped = "\"";
    for (char c : arg) {
        if (c == '\"' || c == '\\' || c == '$' || c == '`') {
            escaped += '\\';
        }
        escaped += c;
    }
    escaped += "\"";
    return escaped;
}

// ---- Helpers UTF-8 -------------------------------------------------------
// O buffer de edição guarda bytes; o cursor precisa andar por codepoint para
// não parar no meio de um caractere acentuado.

inline bool isUtf8Continuation(unsigned char b) { return (b & 0xC0) == 0x80; }

// Posição do início do codepoint anterior a `pos`.
inline size_t utf8Prev(const std::string& s, size_t pos) {
    if (pos == 0) return 0;
    do { --pos; } while (pos > 0 && isUtf8Continuation((unsigned char)s[pos]));
    return pos;
}

// Posição logo após o codepoint que começa em/contém `pos`.
inline size_t utf8Next(const std::string& s, size_t pos) {
    if (pos >= s.size()) return s.size();
    ++pos;
    while (pos < s.size() && isUtf8Continuation((unsigned char)s[pos])) ++pos;
    return pos;
}

inline int getCodepoint(const std::string& s, size_t& i) {
    if (i >= s.size()) return 0;
    unsigned char c = s[i];
    int cp = 0;
    int len = 0;
    if (c < 0x80) { cp = c; len = 1; }
    else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; len = 2; }
    else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; len = 3; }
    else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; len = 4; }
    else { len = 1; cp = 0xFFFD; }
    
    for (int j = 1; j < len; ++j) {
        if (i + j < s.size() && isUtf8Continuation((unsigned char)s[i + j])) {
            cp = (cp << 6) | (s[i + j] & 0x3F);
        } else {
            cp = 0xFFFD;
            break;
        }
    }
    i += len;
    return cp;
}

inline bool isFullWidth(int cp) {
    if (cp < 0x1100) return false;
    return (cp >= 0x1100 && cp <= 0x115F) ||
           (cp == 0x2329 || cp == 0x232A) ||
           (cp >= 0x2E80 && cp <= 0xA4CF && cp != 0x303F) ||
           (cp >= 0xAC00 && cp <= 0xD7A3) ||
           (cp >= 0xF900 && cp <= 0xFAFF) ||
           (cp >= 0xFE10 && cp <= 0xFE19) ||
           (cp >= 0xFE30 && cp <= 0xFE6F) ||
           (cp >= 0xFF00 && cp <= 0xFF60) ||
           (cp >= 0xFFE0 && cp <= 0xFFE6) ||
           (cp >= 0x20000 && cp <= 0x2FFFD) ||
           (cp >= 0x30000 && cp <= 0x3FFFD);
}

// Largura de exibição precisa: 1 coluna por codepoint comum, 2 para full-width CJK.
inline size_t displayWidth(const std::string& s, size_t end = std::string::npos) {
    size_t n = std::min(end, s.size());
    size_t w = 0;
    size_t i = 0;
    while (i < n) {
        int cp = getCodepoint(s, i);
        if (cp == 0) continue;
        w += isFullWidth(cp) ? 2 : 1;
    }
    return w;
}

} // namespace cli::util
