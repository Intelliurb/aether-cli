/*
 * Copyright (c) 2026 caq@intelliurb.com
 * PROPRIEDADE DA INTELLIURB - SOMENTE USO INTERNO
 */
#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

#include "Auth.hpp"

using namespace cli::auth;
namespace fs = std::filesystem;

namespace {

std::string hex(const std::array<uint8_t, 32>& d) { return toHex(d.data(), d.size()); }

// Escreve o JSON num arquivo temporário e devolve o caminho.
class TempUsersFile {
public:
    explicit TempUsersFile(const std::string& content) {
        static int counter = 0;
        path_ = (fs::temp_directory_path() /
                 ("aethercli_auth_test_" + std::to_string(::getpid()) + "_" + std::to_string(counter++) + ".json"))
                    .string();
        std::ofstream out(path_);
        out << content;
    }
    ~TempUsersFile() { std::remove(path_.c_str()); }
    const std::string& path() const { return path_; }

private:
    std::string path_;
};

} // namespace

TEST_CASE("Sha256: vetores de teste NIST") {
    REQUIRE(hex(Sha256::hash(reinterpret_cast<const uint8_t*>("abc"), 3)) ==
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    REQUIRE(hex(Sha256::hash(reinterpret_cast<const uint8_t*>(""), 0)) ==
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    const std::string long_msg = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    REQUIRE(hex(Sha256::hash(reinterpret_cast<const uint8_t*>(long_msg.data()), long_msg.size())) ==
            "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST_CASE("hmacSha256: vetor RFC 4231 (caso 2)") {
    const std::string key = "Jefe";
    const std::string msg = "what do ya want for nothing?";
    auto mac = hmacSha256(reinterpret_cast<const uint8_t*>(key.data()), key.size(),
                          reinterpret_cast<const uint8_t*>(msg.data()), msg.size());
    REQUIRE(hex(mac) == "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843");
}

TEST_CASE("pbkdf2Sha256: vetores conhecidos (password/salt)") {
    std::vector<uint8_t> salt = {'s', 'a', 'l', 't'};
    REQUIRE(hex(pbkdf2Sha256("password", salt, 1)) ==
            "120fb6cffcf8b32c43e7225256c4f837a86548c92ccc35480805987cb70be17b");
    REQUIRE(hex(pbkdf2Sha256("password", salt, 2)) ==
            "ae4d0c95af6b46d32d0adff928f06dd02a303f8ef3c251dfd6e2d85a95474c43");
}

TEST_CASE("toHex/fromHex: ida e volta, entradas inválidas") {
    std::vector<uint8_t> data = {0x00, 0xde, 0xad, 0xbe, 0xef, 0xff};
    REQUIRE(toHex(data) == "00deadbeefff");
    REQUIRE(fromHex("00deadbeefff") == data);
    REQUIRE(fromHex("abc").empty());  // tamanho ímpar
    REQUIRE(fromHex("zz").empty());   // dígito inválido
    REQUIRE(fromHex("").empty());
}

TEST_CASE("constantTimeEqual: iguais, diferentes, tamanhos distintos") {
    uint8_t a[] = {1, 2, 3};
    uint8_t b[] = {1, 2, 3};
    uint8_t c[] = {1, 2, 4};
    REQUIRE(constantTimeEqual(a, 3, b, 3));
    REQUIRE_FALSE(constantTimeEqual(a, 3, c, 3));
    REQUIRE_FALSE(constantTimeEqual(a, 2, b, 3));
}

TEST_CASE("Auth::verify: senha correta, errada, usuário desconhecido, pepper errado") {
    auto rec = Auth::makeRecord("carlos", "s3cret", "pimenta", 1000);
    REQUIRE(rec.salt.size() == 16);
    REQUIRE(rec.hash.size() == 32);
    std::vector<UserRecord> users{rec};

    REQUIRE(Auth::verify(users, "carlos", "s3cret", "pimenta"));
    REQUIRE_FALSE(Auth::verify(users, "carlos", "errada", "pimenta"));
    REQUIRE_FALSE(Auth::verify(users, "bob", "s3cret", "pimenta"));
    REQUIRE_FALSE(Auth::verify(users, "carlos", "s3cret", ""));
    REQUIRE_FALSE(Auth::verify(users, "", "", "pimenta"));
}

TEST_CASE("Auth::makeRecord: salts aleatórios geram hashes distintos") {
    auto r1 = Auth::makeRecord("u", "mesma-senha", "", 100);
    auto r2 = Auth::makeRecord("u", "mesma-senha", "", 100);
    REQUIRE(r1.salt != r2.salt);
    REQUIRE(r1.hash != r2.hash);
}

TEST_CASE("Auth::loadUsers: arquivo válido") {
    auto rec = Auth::makeRecord("carlos", "s3cret", "", 500);
    TempUsersFile f(std::string("{\"users\":[{\"username\":\"carlos\",\"salt\":\"") +
                    toHex(rec.salt) + "\",\"hash\":\"" + toHex(rec.hash) +
                    "\",\"iterations\":500}]}");
    AuthError err = AuthError::BadCredentials;
    auto users = Auth::loadUsers(f.path(), err);
    REQUIRE(err == AuthError::None);
    REQUIRE(users.size() == 1);
    REQUIRE(Auth::verify(users, "carlos", "s3cret", ""));
}

TEST_CASE("Auth::loadUsers: erros mapeados na tabela") {
    AuthError err = AuthError::None;

    Auth::loadUsers("/nonexistent/users.json", err);
    REQUIRE(err == AuthError::UsersFileMissing);

    TempUsersFile broken("{ not json");
    Auth::loadUsers(broken.path(), err);
    REQUIRE(err == AuthError::UsersFileInvalid);

    TempUsersFile empty_list("{\"users\":[]}");
    Auth::loadUsers(empty_list.path(), err);
    REQUIRE(err == AuthError::UsersFileInvalid);

    TempUsersFile bad_hex("{\"users\":[{\"username\":\"x\",\"salt\":\"zz\",\"hash\":\"zz\"}]}");
    Auth::loadUsers(bad_hex.path(), err);
    REQUIRE(err == AuthError::UsersFileInvalid);
}

TEST_CASE("Auth::upsertUserFile: cria o arquivo e calcula seu SHA-256") {
    TempUsersFile f("");
    std::remove(f.path().c_str());
    const auto rec = Auth::makeRecord("carlos", "senha", "", 10);
    bool replaced = true;
    bool recoveredMalformed = true;

    REQUIRE(Auth::upsertUserFile(f.path(), rec, replaced, recoveredMalformed));
    REQUIRE_FALSE(replaced);
    REQUIRE_FALSE(recoveredMalformed);
    REQUIRE(Auth::fileSha256(f.path()).size() == 64);

    AuthError err = AuthError::UsersFileInvalid;
    const auto users = Auth::loadUsers(f.path(), err);
    REQUIRE(err == AuthError::None);
    REQUIRE(users.size() == 1);
    REQUIRE(users.front().username == "carlos");
}

TEST_CASE("Auth::upsertUserFile: substitui arquivo malformado pelo novo usuário") {
    TempUsersFile f("{ malformed");
    const auto rec = Auth::makeRecord("novo", "senha", "", 10);
    bool replaced = false;
    bool recoveredMalformed = false;

    REQUIRE(Auth::upsertUserFile(f.path(), rec, replaced, recoveredMalformed));
    REQUIRE_FALSE(replaced);
    REQUIRE(recoveredMalformed);

    AuthError err = AuthError::UsersFileInvalid;
    const auto users = Auth::loadUsers(f.path(), err);
    REQUIRE(err == AuthError::None);
    REQUIRE(users.size() == 1);
    REQUIRE(users.front().username == "novo");
}

TEST_CASE("Auth::upsertUserFile: recusa users.json que seja symlink") {
    TempUsersFile target("{\"users\":[]}");
    const fs::path link = fs::temp_directory_path() / ("aethercli_auth_link_" + std::to_string(::getpid()));
    fs::remove(link);
    fs::create_symlink(target.path(), link);

    bool replaced = false;
    bool recoveredMalformed = false;
    const auto rec = Auth::makeRecord("novo", "senha", "", 10);
    REQUIRE_FALSE(Auth::upsertUserFile(link.string(), rec, replaced, recoveredMalformed));
    fs::remove(link);

    AuthError err = AuthError::None;
    Auth::loadUsers(target.path(), err);
    REQUIRE(err == AuthError::UsersFileInvalid);
}

TEST_CASE("Tabela de erros: mensagens exigidas pela especificação") {
    REQUIRE(std::string(errorEntry(AuthError::BadCredentials).message) ==
            "cannot log you in, try again");
    REQUIRE(std::string(errorEntry(AuthError::BadCredentials).tag) == "AUTH_FAIL");
    REQUIRE(std::string(errorEntry(AuthError::TooManyAttempts).tag) == "AUTH_LOCKED");
}
