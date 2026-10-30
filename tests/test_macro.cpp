/*
 * Copyright (c) 2026 caq@intelliurb.com
 */
#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "MacroEngine.hpp"

using cli::macro::MacroEngine;
namespace fs = std::filesystem;

TEST_CASE("loadMacro: ignora comentários e linhas vazias") {
    std::string path = (fs::temp_directory_path() / "aethercli_test_macro.macro").string();
    {
        std::ofstream out(path);
        out << "# comentário\n\nshow version\n\n# outro\nshow files\n";
    }

    auto commands = MacroEngine::loadMacro(path);
    std::remove(path.c_str());

    REQUIRE(commands.has_value());
    REQUIRE(*commands == std::vector<std::string>{"show version", "show files"});
}

TEST_CASE("loadMacro: arquivo inexistente retorna nullopt") {
    auto commands = MacroEngine::loadMacro("/nonexistent/file.macro");
    REQUIRE_FALSE(commands.has_value());
}

TEST_CASE("loadMacro: arquivo vazio retorna lista vazia (não nullopt)") {
    std::string path = (fs::temp_directory_path() / "aethercli_test_empty.macro").string();
    { std::ofstream out(path); }

    auto commands = MacroEngine::loadMacro(path);
    std::remove(path.c_str());

    REQUIRE(commands.has_value());
    REQUIRE(commands->empty());
}
