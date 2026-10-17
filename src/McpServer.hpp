#pragma once

#ifdef AETHERCLI_ENABLE_MCP

#include <vector>
#include <string>
#include "CommandParser.hpp"
#include "LocalizationManager.hpp"

namespace cli::mcp {

int runMcpServer(
    const std::vector<cli::parser::Command>& commands,
    cli::i18n::LocalizationManager& lang,
    const std::string& configDir,
    const std::string& configFile
);

} // namespace cli::mcp

#endif // AETHERCLI_ENABLE_MCP
