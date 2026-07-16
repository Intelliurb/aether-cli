/*
 * Copyright (c) 2026 caq@intelliurb.com
 */
#pragma once
#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <csignal>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <termios.h>
#include <unistd.h>
#include <sys/stat.h>
#include <nlohmann/json.hpp>
#include "LocalizationManager.hpp"

namespace cli::auth {

// ---------------------------------------------------------------------------
// Tabela de erros de autenticação: enum + tag (para log) + mensagem (para UI).
// ---------------------------------------------------------------------------
enum class AuthError {
    None = 0,
    BadCredentials,
    TooManyAttempts,
    UsersFileMissing,
    UsersFileInvalid,
    EmptyPassword,
    PasswordMismatch,
};

struct AuthErrorEntry {
    AuthError code;
    const char* tag;     // identificador curto para linhas de log
    const char* message; // mensagem mostrada ao usuário
};

inline constexpr AuthErrorEntry AUTH_ERROR_TABLE[] = {
    {AuthError::None,             "OK",            "success"},
    {AuthError::BadCredentials,   "AUTH_FAIL",     "cannot log you in, try again"},
    {AuthError::TooManyAttempts,  "AUTH_LOCKED",   "too many failed login attempts"},
    {AuthError::UsersFileMissing, "USERS_MISSING", "users file not found"},
    {AuthError::UsersFileInvalid, "USERS_INVALID", "users file is malformed"},
    {AuthError::EmptyPassword,    "PW_EMPTY",      "password cannot be empty"},
    {AuthError::PasswordMismatch, "PW_MISMATCH",   "passwords do not match"},
};

inline const AuthErrorEntry& errorEntry(AuthError e) {
    for (const auto& entry : AUTH_ERROR_TABLE)
        if (entry.code == e) return entry;
    return AUTH_ERROR_TABLE[0];
}

// ---------------------------------------------------------------------------
// SHA-256 (FIPS 180-4) autocontido — o projeto não linka biblioteca de crypto.
// Primitivas simétricas/hash seguem seguras contra ataques quânticos
// conhecidos (Grover apenas reduz a margem pela metade).
// ---------------------------------------------------------------------------
class Sha256 {
public:
    Sha256() { reset(); }

    void reset() {
        h_ = {0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
              0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
        len_ = 0;
        buf_len_ = 0;
    }

    void update(const uint8_t* data, size_t n) {
        len_ += n;
        while (n > 0) {
            size_t take = std::min(n, size_t(64) - buf_len_);
            std::memcpy(buf_ + buf_len_, data, take);
            buf_len_ += take;
            data += take;
            n -= take;
            if (buf_len_ == 64) {
                compress(buf_);
                buf_len_ = 0;
            }
        }
    }

    std::array<uint8_t, 32> digest() {
        uint64_t bitlen = len_ * 8;
        const uint8_t pad = 0x80, zero = 0x00;
        update(&pad, 1);
        while (buf_len_ != 56) update(&zero, 1);
        uint8_t lenb[8];
        for (int i = 0; i < 8; ++i) lenb[i] = uint8_t(bitlen >> (56 - 8 * i));
        update(lenb, 8);

        std::array<uint8_t, 32> out{};
        for (int i = 0; i < 8; ++i) {
            out[4 * i + 0] = uint8_t(h_[i] >> 24);
            out[4 * i + 1] = uint8_t(h_[i] >> 16);
            out[4 * i + 2] = uint8_t(h_[i] >> 8);
            out[4 * i + 3] = uint8_t(h_[i]);
        }
        return out;
    }

    static std::array<uint8_t, 32> hash(const uint8_t* data, size_t n) {
        Sha256 s;
        s.update(data, n);
        return s.digest();
    }

private:
    static uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

    void compress(const uint8_t* p) {
        static constexpr uint32_t K[64] = {
            0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
            0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
            0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
            0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
            0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
            0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
            0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
            0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (uint32_t(p[4 * i]) << 24) | (uint32_t(p[4 * i + 1]) << 16) |
                   (uint32_t(p[4 * i + 2]) << 8) | uint32_t(p[4 * i + 3]);
        }
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3];
        uint32_t e = h_[4], f = h_[5], g = h_[6], h = h_[7];
        for (int i = 0; i < 64; ++i) {
            uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            uint32_t ch = (e & f) ^ (~e & g);
            uint32_t t1 = h + S1 + ch + K[i] + w[i];
            uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t t2 = S0 + maj;
            h = g; g = f; f = e; e = d + t1;
            d = c; c = b; b = a; a = t1 + t2;
        }
        h_[0] += a; h_[1] += b; h_[2] += c; h_[3] += d;
        h_[4] += e; h_[5] += f; h_[6] += g; h_[7] += h;
    }

    std::array<uint32_t, 8> h_{};
    uint64_t len_ = 0;
    uint8_t buf_[64] = {};
    size_t buf_len_ = 0;
};

inline std::array<uint8_t, 32> hmacSha256(const uint8_t* key, size_t keylen,
                                          const uint8_t* msg, size_t msglen) {
    uint8_t k[64] = {};
    if (keylen > 64) {
        auto kh = Sha256::hash(key, keylen);
        std::memcpy(k, kh.data(), kh.size());
    } else if (keylen > 0) {
        std::memcpy(k, key, keylen);
    }
    uint8_t ipad[64], opad[64];
    for (int i = 0; i < 64; ++i) {
        ipad[i] = k[i] ^ 0x36;
        opad[i] = k[i] ^ 0x5c;
    }
    Sha256 inner;
    inner.update(ipad, 64);
    if (msglen > 0) inner.update(msg, msglen);
    auto ih = inner.digest();
    Sha256 outer;
    outer.update(opad, 64);
    outer.update(ih.data(), ih.size());
    return outer.digest();
}

// PBKDF2-HMAC-SHA256 com dkLen fixo em 32 bytes (um único bloco T_1).
inline std::array<uint8_t, 32> pbkdf2Sha256(const std::string& password,
                                            const std::vector<uint8_t>& salt,
                                            uint32_t iterations) {
    std::vector<uint8_t> s = salt;
    s.insert(s.end(), {0x00, 0x00, 0x00, 0x01}); // INT(1) big-endian
    auto u = hmacSha256(reinterpret_cast<const uint8_t*>(password.data()), password.size(),
                        s.data(), s.size());
    auto t = u;
    for (uint32_t i = 1; i < iterations; ++i) {
        u = hmacSha256(reinterpret_cast<const uint8_t*>(password.data()), password.size(),
                       u.data(), u.size());
        for (size_t j = 0; j < t.size(); ++j) t[j] ^= u[j];
    }
    return t;
}

// ---------------------------------------------------------------------------
// Utilitários
// ---------------------------------------------------------------------------
inline std::string toHex(const uint8_t* data, size_t n) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(n * 2);
    for (size_t i = 0; i < n; ++i) {
        out += digits[data[i] >> 4];
        out += digits[data[i] & 0x0F];
    }
    return out;
}

inline std::string toHex(const std::vector<uint8_t>& v) { return toHex(v.data(), v.size()); }

// Retorna vetor vazio se a string não for hex válido (tratado como arquivo inválido).
inline std::vector<uint8_t> fromHex(const std::string& s) {
    if (s.empty() || s.size() % 2 != 0) return {};
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    std::vector<uint8_t> out(s.size() / 2);
    for (size_t i = 0; i < out.size(); ++i) {
        int hi = nibble(s[2 * i]), lo = nibble(s[2 * i + 1]);
        if (hi < 0 || lo < 0) return {};
        out[i] = uint8_t((hi << 4) | lo);
    }
    return out;
}

inline std::vector<uint8_t> randomBytes(size_t n) {
    std::vector<uint8_t> out(n);
    std::ifstream ur("/dev/urandom", std::ios::binary);
    if (!ur.read(reinterpret_cast<char*>(out.data()), std::streamsize(n))) {
        throw std::runtime_error("Cannot read /dev/urandom for salt generation");
    }
    return out;
}

// Comparação em tempo constante: evita vazar por timing quantos bytes conferem.
inline bool constantTimeEqual(const uint8_t* a, size_t alen, const uint8_t* b, size_t blen) {
    if (alen != blen) return false;
    uint8_t diff = 0;
    for (size_t i = 0; i < alen; ++i) diff |= uint8_t(a[i] ^ b[i]);
    return diff == 0;
}

// ---------------------------------------------------------------------------
// Registro de usuário e operações de autenticação.
// Arquivo: <configDir>/users.json
//   { "users": [ { "username", "salt" (hex), "hash" (hex), "iterations" } ] }
// Hash: PBKDF2-HMAC-SHA256(senha + pepper, salt aleatório de 16 bytes).
// Pepper: variável de ambiente AETHERCLI_PEPPER (segredo fora do arquivo).
// ---------------------------------------------------------------------------
struct UserRecord {
    std::string username;
    std::vector<uint8_t> salt;
    std::vector<uint8_t> hash;
    uint32_t iterations = 0;
};

// Recomendação OWASP atual para PBKDF2-HMAC-SHA256.
inline constexpr uint32_t DEFAULT_ITERATIONS = 600000;

class Auth {
public:
    static std::string pepperFromEnv() {
        const char* p = std::getenv("AETHERCLI_PEPPER");
        return p ? p : "";
    }

    static std::vector<UserRecord> loadUsers(const std::string& path, AuthError& err) {
        err = AuthError::None;
        std::ifstream file(path);
        if (!file.is_open()) {
            err = AuthError::UsersFileMissing;
            return {};
        }

        nlohmann::json j;
        try { file >> j; } catch (const nlohmann::json::parse_error&) { err = AuthError::UsersFileInvalid; return {}; }
        if (!j.contains("users") || !j["users"].is_array()) { err = AuthError::UsersFileInvalid; return {}; }
        std::vector<UserRecord> users;
        for (const auto& u : j["users"]) {
            if (!u.is_object() || !u.contains("username") || !u["username"].is_string() ||
                !u.contains("salt") || !u["salt"].is_string() || !u.contains("hash") || !u["hash"].is_string()) {
                err = AuthError::UsersFileInvalid; return {};
            }
            UserRecord r;
            r.username = u["username"];
            r.salt = fromHex(u["salt"]);
            r.hash = fromHex(u["hash"]);
            r.iterations = u.value("iterations", DEFAULT_ITERATIONS);
            if (r.username.empty() || r.salt.empty() || r.hash.size() != 32 || r.iterations == 0) {
                err = AuthError::UsersFileInvalid; return {};
            }
            users.push_back(std::move(r));
        }
        if (users.empty()) err = AuthError::UsersFileInvalid;
        return users;
    }

    static bool verify(const std::vector<UserRecord>& users, const std::string& username,
                       const std::string& password, const std::string& pepper) {
        const UserRecord* rec = nullptr;
        for (const auto& u : users) {
            if (u.username == username) {
                rec = &u;
                break;
            }
        }
        
        // Usuário desconhecido: roda o PBKDF2 com salt/hash dummy mesmo assim,
        // para não revelar por timing se o usuário existe.
        const std::vector<uint8_t> dummy_hash(32, 0x00);
        const std::vector<uint8_t> dummy_salt(16, 0xAA);
        const std::vector<uint8_t>& hash = rec ? rec->hash : dummy_hash;
        const std::vector<uint8_t>& salt = rec ? rec->salt : dummy_salt;
        uint32_t iterations = rec ? rec->iterations : DEFAULT_ITERATIONS;

        auto dk = pbkdf2Sha256(password + pepper, salt, iterations);
        bool ok = constantTimeEqual(dk.data(), dk.size(), hash.data(), hash.size());
        return rec != nullptr && ok;
    }

    static UserRecord makeRecord(const std::string& username, const std::string& password,
                                 const std::string& pepper,
                                 uint32_t iterations = DEFAULT_ITERATIONS) {
        UserRecord r;
        r.username = username;
        r.salt = randomBytes(16);
        r.iterations = iterations;
        auto dk = pbkdf2Sha256(password + pepper, r.salt, iterations);
        r.hash.assign(dk.begin(), dk.end());
        return r;
    }

    // Lê uma linha direto do fd 0. Permite ocultar digitação (senha).
    // Aborta instantaneamente e retorna false caso ESC seja pressionado.
    static bool readLineFd(std::string& out, bool hidden = false, const std::string& prompt = "") {
        out.clear();
        struct termios oldt {}, newt {};
        bool tty = isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &oldt) == 0;
        if (tty) {
            newt = oldt;
            newt.c_lflag &= ~(ICANON | ECHO);
            tcsetattr(STDIN_FILENO, TCSAFLUSH, &newt);
        }
        if (!prompt.empty()) {
            std::cout << prompt << std::flush;
        }
        bool aborted = false;
        for (;;) {
            char c;
            ssize_t n = ::read(STDIN_FILENO, &c, 1);
            if (n == 1) {
                if (c == '\x1b') {
                    aborted = true;
                    break;
                }
                if (c == '\n' || c == '\r') {
                    if (tty) std::cout << std::endl;
                    break;
                }
                if (c == 127 || c == '\b') {
                    if (!out.empty()) {
                        out.pop_back();
                        if (tty && !hidden) std::cout << "\b \b" << std::flush;
                    }
                } else if (isprint((unsigned char)c) || (c & 0x80)) {
                    out += c;
                    if (tty && !hidden) std::cout << c << std::flush;
                }
            } else if (n == -1 && errno == EINTR) {
                continue;
            } else {
                if (out.empty()) aborted = true;
                break;
            }
        }
        if (tty) {
            tcsetattr(STDIN_FILENO, TCSAFLUSH, &oldt);
        }
        return !aborted;
    }

    // Lê uma linha com echo desligado (senha). Fora de TTY lê normalmente.
    static bool readHiddenLine(const std::string& prompt, std::string& out) {
        auto old_int = std::signal(SIGINT, SIG_IGN);
        bool res = readLineFd(out, true, prompt);
        std::signal(SIGINT, old_int);
        return res;
    }

    // Provisionamento via CLI (--adduser): cria/atualiza usuário no users.json.
    static bool addUser(const std::string& usersFile, const std::string& username, const cli::i18n::LocalizationManager& lang) {
        if (username.length() < 1 || username.length() > 32) {
            std::cerr << lang.getOr("auth_err_username_len", "%% Error: Username must be between 1 and 32 characters long.") << std::endl;
            return false;
        }

        // Validação de caracteres padrão do Unix
        for (size_t i = 0; i < username.length(); ++i) {
            char c = username[i];
            if (i == 0) {
                if (!(c >= 'a' && c <= 'z') && c != '_') {
                    std::cerr << lang.getOr("auth_err_username_start", "%% Error: Username must start with a lowercase letter or underscore.") << std::endl;
                    return false;
                }
            } else {
                if (!(c >= 'a' && c <= 'z') && !(c >= '0' && c <= '9') && c != '_' && c != '-') {
                    std::cerr << lang.getOr("auth_err_username_invalid", "%% Error: Username contains invalid characters.") << std::endl;
                    return false;
                }
            }
        }
        std::cout << lang.getOr("auth_msg_pw_criteria", "Password must be between 8 and 255 characters long.") << std::endl;
        std::string p1;
        
        int attempts = 0;
        bool success = false;
        while (attempts < 3) {
            if (!readHiddenLine(lang.getOr("auth_prompt_new_pw", "New password: "), p1)) {
                std::cout << std::endl;
                return false;
            }
            if (p1.length() < 8 || p1.length() > 255) {
                std::cerr << lang.getOr("auth_err_pw_len", "%% Error: Password must be between 8 and 255 characters long.") << std::endl;
                attempts++;
                continue;
            }
            std::string p2;
            if (!readHiddenLine(lang.getOr("auth_prompt_retype_pw", "Retype password: "), p2)) {
                std::cout << std::endl;
                return false;
            }
            if (p1 != p2) {
                std::cerr << lang.getOr("auth_err_pw_mismatch", "%% Error: passwords do not match") << std::endl;
                attempts++;
                continue;
            }
            success = true;
            break;
        }
        
        if (!success) {
            std::cerr << lang.getOr("auth_err_pw_attempts", "%% Error: Too many failed attempts.") << std::endl;
            return false;
        }

        UserRecord rec = makeRecord(username, p1, pepperFromEnv());

        nlohmann::json j = nlohmann::json::object();
        {
            std::ifstream in(usersFile);
            if (in.is_open()) {
                try {
                    in >> j;
                } catch (const nlohmann::json::parse_error&) {
                    std::cerr << lang.getOr("auth_err_users_invalid", "%% Error: users file is malformed")
                              << ": " << usersFile << std::endl;
                    return false;
                }
            }
        }
        if (!j.contains("users") || !j["users"].is_array()) j["users"] = nlohmann::json::array();

        nlohmann::json entry = {{"username", rec.username},
                                {"salt", toHex(rec.salt)},
                                {"hash", toHex(rec.hash)},
                                {"iterations", rec.iterations}};
        bool replaced = false;
        for (auto& u : j["users"]) {
            if (u.is_object() && u.value("username", "") == username) {
                u = entry;
                replaced = true;
                break;
            }
        }
        if (!replaced) j["users"].push_back(entry);

        std::ofstream out(usersFile);
        if (!out) {
            std::cerr << "%% Error: cannot write " << usersFile << " (" << std::strerror(errno) << ")" << std::endl;
            return false;
        }
        out << j.dump(4) << std::endl;
        ::chmod(usersFile.c_str(), 0600); // hashes de senha não devem ser públicos
        std::cout << (replaced ? lang.getOr("auth_msg_user_updated", "Updated user '") : lang.getOr("auth_msg_user_added", "Added user '")) << username
                  << lang.getOr("auth_msg_in", "' in ") << usersFile << std::endl;
        return true;
    }
};

} // namespace cli::auth
