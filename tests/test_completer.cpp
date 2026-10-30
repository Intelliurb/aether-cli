/*
 * Copyright (c) 2026 caq@intelliurb.com
 */
#include <catch2/catch_test_macros.hpp>

#include "Completer.hpp"

using cli::parser::Command;
using namespace cli::completer;

static std::vector<Command> makeTree() {
    Command iface;
    iface.name = "interface";
    iface.activation = "ip addr show";

    Command route;
    route.name = "route";
    route.activation = "ip route show";

    Command ip;
    ip.name = "ip";
    ip.subcommands = {iface, route};

    Command files;
    files.name = "files";
    files.activation = "ls -la";
    files.allow_args = true;

    Command show;
    show.name = "show";
    show.subcommands = {ip, files};

    Command shutdown;
    shutdown.name = "shutdown";
    shutdown.activation = "true";

    return {show, shutdown};
}

TEST_CASE("resolve: caminho exato até a folha") {
    auto tree = makeTree();
    auto r = resolve(tree, {"show", "ip", "route"});
    REQUIRE(r.cmd != nullptr);
    REQUIRE(r.cmd->name == "route");
    REQUIRE(r.depth == 3);
    REQUIRE_FALSE(r.ambiguous);
}

TEST_CASE("resolve: abreviação única estilo Cisco") {
    auto tree = makeTree();
    auto r = resolve(tree, {"sho", "i", "ro"});
    REQUIRE(r.cmd != nullptr);
    REQUIRE(r.cmd->name == "route");
    REQUIRE(r.depth == 3);
}

TEST_CASE("resolve: abreviação ambígua é sinalizada") {
    auto tree = makeTree();
    auto r = resolve(tree, {"sh"}); // show | shutdown
    REQUIRE(r.cmd == nullptr);
    REQUIRE(r.ambiguous);
    REQUIRE(r.ambiguous_token == "sh");
}

TEST_CASE("resolve: match exato tem prioridade sobre prefixo") {
    Command a;
    a.name = "show";
    Command b;
    b.name = "show-all";
    std::vector<Command> tree = {a, b};

    auto r = resolve(tree, {"show"});
    REQUIRE(r.cmd != nullptr);
    REQUIRE(r.cmd->name == "show");
    REQUIRE_FALSE(r.ambiguous);
}

TEST_CASE("resolve: tokens além da folha viram argumentos (depth para na folha)") {
    auto tree = makeTree();
    auto r = resolve(tree, {"show", "files", "/tmp"});
    REQUIRE(r.cmd != nullptr);
    REQUIRE(r.cmd->name == "files");
    REQUIRE(r.depth == 2);
    REQUIRE_FALSE(r.ambiguous);
}

TEST_CASE("resolve: token desconhecido interrompe sem ambiguidade") {
    auto tree = makeTree();
    auto r = resolve(tree, {"show", "xyz", "interface"});
    REQUIRE(r.cmd != nullptr);
    REQUIRE(r.cmd->name == "show");
    REQUIRE(r.depth == 1);
    REQUIRE_FALSE(r.ambiguous);
}

TEST_CASE("prefixMatches: filtra por prefixo; prefixo vazio casa tudo") {
    auto tree = makeTree();
    REQUIRE(prefixMatches(tree, "sh").size() == 2);
    REQUIRE(prefixMatches(tree, "shu").size() == 1);
    REQUIRE(prefixMatches(tree, "").size() == 2);
    REQUIRE(prefixMatches(tree, "xyz").empty());
}

TEST_CASE("longestCommonPrefix: completa até onde os matches concordam") {
    auto tree = makeTree();
    auto m = prefixMatches(tree, "s");
    REQUIRE(longestCommonPrefix(m) == "sh"); // show | shutdown

    auto one = prefixMatches(tree, "shu");
    REQUIRE(longestCommonPrefix(one) == "shutdown");

    REQUIRE(longestCommonPrefix({}).empty());
}

TEST_CASE("prefixMatchStrings e longestCommonPrefixStrings (PAR-3)") {
    std::vector<std::string> candidates = {"staging", "production", "preview"};
    auto m1 = prefixMatchStrings(candidates, "pr");
    REQUIRE(m1.size() == 2);
    REQUIRE(longestCommonPrefixStrings(m1) == "pr");

    auto m2 = prefixMatchStrings(candidates, "st");
    REQUIRE(m2.size() == 1);
    REQUIRE(longestCommonPrefixStrings(m2) == "staging");

    REQUIRE(prefixMatchStrings(candidates, "xyz").empty());
    REQUIRE(longestCommonPrefixStrings({}).empty());
}

TEST_CASE("resolve: admin_only do pai vale para o caminho inteiro") {
    Command child;
    child.name = "child";
    Command parent;
    parent.name = "parent";
    parent.admin_only = true;
    parent.subcommands = {child};
    std::vector<Command> tree{parent};

    auto r = resolve(tree, {"parent", "child"});
    REQUIRE(r.cmd != nullptr);
    REQUIRE_FALSE(r.cmd->admin_only);
    REQUIRE(r.requires_admin);
}
