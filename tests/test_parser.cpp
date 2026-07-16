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

#include "CommandParser.hpp"

using cli::parser::CommandParser;
namespace fs = std::filesystem;

namespace {

// Escreve o JSON num arquivo temporário e devolve o caminho.
class TempConfig {
public:
    explicit TempConfig(const std::string& content) {
        static int counter = 0;
        path_ = (fs::temp_directory_path() /
                 ("aethercli_test_" + std::to_string(::getpid()) + "_" + std::to_string(counter++) + ".json"))
                    .string();
        std::ofstream out(path_);
        out << content;
    }
    ~TempConfig() { std::remove(path_.c_str()); }
    const std::string& path() const { return path_; }

private:
    std::string path_;
};

} // namespace

TEST_CASE("parseConfig: configuração válida com hierarquia e args") {
    TempConfig cfg(R"({
        "motd": "hello",
        "commands": [
            {
                "name": "show",
                "short_desc": "desc",
                "subcommands": [
                    {"name": "files", "activation": "ls -la", "args": "append", "syntax": "show files [path]"},
                    {"name": "version", "activation": "uname -a"}
                ]
            }
        ]
    })");

    auto config = CommandParser::parseConfig(cfg.path());
    REQUIRE(config.motd == "hello");
    REQUIRE(config.commands.size() == 1);

    const auto& show = config.commands[0];
    REQUIRE(show.name == "show");
    REQUIRE(show.subcommands.size() == 2);
    REQUIRE(show.subcommands[0].allow_args);        // "args": "append"
    REQUIRE_FALSE(show.subcommands[1].allow_args);  // default: none
}

TEST_CASE("parseConfig: arquivo inexistente lança erro") {
    REQUIRE_THROWS(CommandParser::parseConfig("/nonexistent/path/config.json"));
}

TEST_CASE("parseConfig: JSON malformado lança erro") {
    TempConfig cfg("{ not valid json !");
    REQUIRE_THROWS(CommandParser::parseConfig(cfg.path()));
}

TEST_CASE("parseConfig: 'commands' ausente lança erro") {
    TempConfig cfg(R"({"motd": "x"})");
    REQUIRE_THROWS(CommandParser::parseConfig(cfg.path()));
}

TEST_CASE("parseConfig: comando sem 'name' lança erro") {
    TempConfig cfg(R"({"commands": [{"activation": "ls"}]})");
    REQUIRE_THROWS(CommandParser::parseConfig(cfg.path()));
}

TEST_CASE("parseConfig: valor inválido em 'args' lança erro") {
    TempConfig cfg(R"({"commands": [{"name": "x", "activation": "ls", "args": "yes"}]})");
    REQUIRE_THROWS(CommandParser::parseConfig(cfg.path()));
}

TEST_CASE("parseConfig: 'subcommands' que não é array lança erro") {
    TempConfig cfg(R"({"commands": [{"name": "x", "subcommands": "oops"}]})");
    REQUIRE_THROWS(CommandParser::parseConfig(cfg.path()));
}

TEST_CASE("parseConfig: parse de restricted_session") {
    TempConfig cfg1(R"({"commands": [{"name": "x"}], "restricted_session": true})");
    auto config1 = CommandParser::parseConfig(cfg1.path());
    REQUIRE(config1.restricted_session == true);

    TempConfig cfg2(R"({"commands": [{"name": "x"}], "restricted_session": "true"})");
    auto config2 = CommandParser::parseConfig(cfg2.path());
    REQUIRE(config2.restricted_session == true);

    TempConfig cfg3(R"({"commands": [{"name": "x"}], "restricted_session": false})");
    auto config3 = CommandParser::parseConfig(cfg3.path());
    REQUIRE(config3.restricted_session == false);

    TempConfig cfg4(R"({"commands": [{"name": "x"}], "restricted_session": "invalid"})");
    REQUIRE_THROWS(CommandParser::parseConfig(cfg4.path()));
}

TEST_CASE("parseConfig: parse de require_authentication") {
    TempConfig cfg1(R"({"commands": [{"name": "x"}], "require_authentication": true})");
    auto config1 = CommandParser::parseConfig(cfg1.path());
    REQUIRE(config1.require_authentication == true);

    TempConfig cfg2(R"({"commands": [{"name": "x"}], "require_authentication": "true"})");
    auto config2 = CommandParser::parseConfig(cfg2.path());
    REQUIRE(config2.require_authentication == true);

    TempConfig cfg3(R"({"commands": [{"name": "x"}], "require_authentication": false})");
    auto config3 = CommandParser::parseConfig(cfg3.path());
    REQUIRE(config3.require_authentication == false);

    TempConfig cfg4(R"({"commands": [{"name": "x"}], "require_authentication": "invalid"})");
    REQUIRE_THROWS(CommandParser::parseConfig(cfg4.path()));
}
