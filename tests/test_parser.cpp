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

TEST_CASE("parseConfig: novos campos de comando timeout_s, max_output_bytes, cwd e env") {
    TempConfig cfg(R"({
        "commands": [
            {
                "name": "run",
                "activation": "echo test",
                "timeout_s": 15,
                "max_output_bytes": 4096,
                "cwd": "/tmp",
                "env": {
                    "FOO": "bar",
                    "BAZ": "qux"
                }
            }
        ]
    })");

    auto config = CommandParser::parseConfig(cfg.path());
    REQUIRE(config.commands.size() == 1);
    const auto& cmd = config.commands[0];
    REQUIRE(cmd.name == "run");
    REQUIRE(cmd.timeout_s == 15);
    REQUIRE(cmd.max_output_bytes == 4096);
    REQUIRE(cmd.cwd == "/tmp");
    REQUIRE(cmd.env.at("FOO") == "bar");
    REQUIRE(cmd.env.at("BAZ") == "qux");

    // Validações de erro
    TempConfig cfg_bad_timeout1(R"({"commands": [{"name": "x", "timeout_s": -1}]})");
    REQUIRE_THROWS(CommandParser::parseConfig(cfg_bad_timeout1.path()));

    TempConfig cfg_bad_timeout2(R"({"commands": [{"name": "x", "timeout_s": "10"}]})");
    REQUIRE_THROWS(CommandParser::parseConfig(cfg_bad_timeout2.path()));

    TempConfig cfg_bad_bytes1(R"({"commands": [{"name": "x", "max_output_bytes": -5}]})");
    REQUIRE_THROWS(CommandParser::parseConfig(cfg_bad_bytes1.path()));

    TempConfig cfg_bad_cwd(R"({"commands": [{"name": "x", "cwd": 123}]})");
    REQUIRE_THROWS(CommandParser::parseConfig(cfg_bad_cwd.path()));

    TempConfig cfg_bad_env1(R"({"commands": [{"name": "x", "env": "not an object"}]})");
    REQUIRE_THROWS(CommandParser::parseConfig(cfg_bad_env1.path()));

    TempConfig cfg_bad_env2(R"({"commands": [{"name": "x", "env": {"FOO": 123}}]})");
    REQUIRE_THROWS(CommandParser::parseConfig(cfg_bad_env2.path()));
}

TEST_CASE("parseConfig: params tipados e validacao (PAR-1) e effect (MCP-4)") {
    TempConfig cfg(R"({
        "commands": [
            {
                "name": "deploy",
                "activation": "deploy.sh",
                "effect": "destructive",
                "idempotent": true,
                "open_world": false,
                "params": [
                    {
                        "name": "env",
                        "type": "string",
                        "description": "Target environment",
                        "required": true,
                        "enum": ["staging", "production"]
                    },
                    {
                        "name": "replicas",
                        "type": "integer",
                        "description": "Number of replicas",
                        "required": false,
                        "minimum": 1,
                        "maximum": 10
                    },
                    {
                        "name": "force",
                        "type": "boolean",
                        "description": "Force deployment"
                    }
                ]
            }
        ]
    })");

    auto config = CommandParser::parseConfig(cfg.path());
    REQUIRE(config.commands.size() == 1);
    const auto& cmd = config.commands[0];
    REQUIRE(cmd.name == "deploy");
    REQUIRE(cmd.effect == "destructive");
    REQUIRE(cmd.idempotent == true);
    REQUIRE(cmd.open_world == false);
    REQUIRE(cmd.params.size() == 3);

    REQUIRE(cmd.params[0].name == "env");
    REQUIRE(cmd.params[0].type == "string");
    REQUIRE(cmd.params[0].required == true);
    REQUIRE(cmd.params[0].enum_values.size() == 2);
    REQUIRE(cmd.params[0].enum_values[0] == "staging");
    REQUIRE(cmd.params[0].enum_values[1] == "production");

    REQUIRE(cmd.params[1].name == "replicas");
    REQUIRE(cmd.params[1].type == "integer");
    REQUIRE(cmd.params[1].required == false);
    REQUIRE(cmd.params[1].minimum.value() == 1);
    REQUIRE(cmd.params[1].maximum.value() == 10);

    REQUIRE(cmd.params[2].name == "force");
    REQUIRE(cmd.params[2].type == "boolean");

    // Validações de erro
    // Tipo desconhecido
    TempConfig cfg_bad_type(R"({"commands": [{"name": "x", "params": [{"name": "p", "type": "float"}]}]})");
    REQUIRE_THROWS(CommandParser::parseConfig(cfg_bad_type.path()));

    // Enum vazio
    TempConfig cfg_empty_enum(R"({"commands": [{"name": "x", "params": [{"name": "p", "enum": []}]}]})");
    REQUIRE_THROWS(CommandParser::parseConfig(cfg_empty_enum.path()));

    // Enum com duplicatas
    TempConfig cfg_dup_enum(R"({"commands": [{"name": "x", "params": [{"name": "p", "enum": ["a", "a"]}]}]})");
    REQUIRE_THROWS(CommandParser::parseConfig(cfg_dup_enum.path()));

    // Nome repetido
    TempConfig cfg_dup_name(R"({"commands": [{"name": "x", "params": [{"name": "p"}, {"name": "p"}]}]})");
    REQUIRE_THROWS(CommandParser::parseConfig(cfg_dup_name.path()));

    // minimum > maximum
    TempConfig cfg_bad_range(R"({"commands": [{"name": "x", "params": [{"name": "p", "type": "integer", "minimum": 10, "maximum": 5}]}]})");
    REQUIRE_THROWS(CommandParser::parseConfig(cfg_bad_range.path()));

    // params e args: append mutuamente exclusivos
    TempConfig cfg_params_and_append(R"({
        "commands": [
            {
                "name": "x",
                "args": "append",
                "params": [{"name": "p", "type": "string"}]
            }
        ]
    })");
    REQUIRE_THROWS(CommandParser::parseConfig(cfg_params_and_append.path()));

    // effect inválido
    TempConfig cfg_bad_effect(R"({"commands": [{"name": "x", "effect": "unknown"}]})");
    REQUIRE_THROWS(CommandParser::parseConfig(cfg_bad_effect.path()));
}

using namespace cli::parser;

TEST_CASE("parseConfig: pattern e nome de parâmetro inválidos falham na carga") {
    TempConfig bad_pattern(R"({"commands": [{"name": "x", "params": [{"name": "p", "pattern": "("}]}]})");
    REQUIRE_THROWS(CommandParser::parseConfig(bad_pattern.path()));

    TempConfig bad_name(R"({"commands": [{"name": "x", "params": [{"name": "a=b"}]}]})");
    REQUIRE_THROWS(CommandParser::parseConfig(bad_name.path()));
}

TEST_CASE("validação: valor com '-' só passa com ativação terminada em ' --'") {
    CommandParam free_text;
    free_text.name = "v";
    CommandParam choice;
    choice.name = "c";
    choice.enum_values = {"-x"};

    std::map<std::string, std::string> env;
    std::vector<std::string> argv;
    REQUIRE(validatePositionalParams({free_text}, {"--evil"}, "cmd", env, argv).has_value());
    REQUIRE_FALSE(validatePositionalParams({free_text}, {"--ok"}, "cmd", env, argv, true).has_value());
    REQUIRE_FALSE(validatePositionalParams({choice}, {"-x"}, "cmd", env, argv).has_value());
    REQUIRE(validateJsonParams({free_text}, nlohmann::json{{"v", "--evil"}}, "cmd", env, argv).has_value());
    CommandParam number;
    number.name = "n";
    number.type = "integer";
    REQUIRE(validatePositionalParams({number}, {"-5"}, "cmd", env, argv).has_value());
    REQUIRE_FALSE(validatePositionalParams({number}, {"-5"}, "cmd", env, argv, true).has_value());
    REQUIRE(validateJsonParams({number}, nlohmann::json{{"n", -5}}, "cmd", env, argv).has_value());
    REQUIRE(activationEndsOptions("printf %s --"));
    REQUIRE_FALSE(activationEndsOptions("printf --x"));
}

TEST_CASE("validação JSON: opcional omitido preserva a posição do seguinte") {
    CommandParam a, b;
    a.name = "a";
    b.name = "b";
    std::map<std::string, std::string> env;
    std::vector<std::string> argv;
    REQUIRE_FALSE(validateJsonParams({a, b}, nlohmann::json{{"b", "B"}}, "cmd", env, argv).has_value());
    REQUIRE(argv == std::vector<std::string>{"", "B"});
    REQUIRE(env.count("AETHERCLI_PARAM_A") == 0);
}

TEST_CASE("validação JSON: inteiro acima de LLONG_MAX é recusado") {
    CommandParam n;
    n.name = "n";
    n.type = "integer";
    std::map<std::string, std::string> env;
    std::vector<std::string> argv;
    REQUIRE(validateJsonParams({n}, nlohmann::json{{"n", 18446744073709551615ULL}}, "cmd", env, argv).has_value());
}
