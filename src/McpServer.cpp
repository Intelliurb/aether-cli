#ifdef AETHERCLI_ENABLE_MCP

#include "McpServer.hpp"
#include "CliUtils.hpp"
#include <iostream>
#include <string>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <sys/wait.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
using namespace cli::parser;

namespace cli::mcp {

struct McpToolMapping {
    std::string toolName;
    std::string commandPath;
    std::string description;
    bool allowArgs;
    std::string syntax;
};

static void collectTools(
    const std::vector<Command>& commands,
    const std::string& prefixName,
    const std::string& prefixPath,
    std::vector<McpToolMapping>& outTools
) {
    for (const auto& cmd : commands) {
        std::string currentName = prefixName.empty() ? cmd.name : prefixName + "_" + cmd.name;
        std::string currentPath = prefixPath.empty() ? cmd.name : prefixPath + " " + cmd.name;

        if (!cmd.activation.empty()) {
            std::string desc = cmd.short_desc.empty() ? cmd.description : cmd.short_desc;
            if (desc.empty()) {
                desc = "Execute: " + currentPath;
            }
            outTools.push_back({
                currentName,
                currentPath,
                desc,
                cmd.allow_args,
                cmd.syntax
            });
        }

        if (!cmd.subcommands.empty()) {
            collectTools(cmd.subcommands, currentName, currentPath, outTools);
        }
    }
}

static json buildToolsListResponse(const std::vector<McpToolMapping>& tools, const json& id) {
    json toolsArray = json::array();

    for (const auto& tool : tools) {
        json toolObj;
        toolObj["name"] = tool.toolName;
        toolObj["description"] = tool.description;

        json properties = json::object();
        json required = json::array();

        if (tool.allowArgs) {
            properties["args"] = {
                {"type", "string"},
                {"description", "Arguments for the command. Syntax: " + (tool.syntax.empty() ? tool.commandPath : tool.syntax)}
            };
        }

        toolObj["inputSchema"] = {
            {"type", "object"},
            {"properties", properties},
            {"required", required}
        };

        toolsArray.push_back(toolObj);
    }

    return {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"result", {
            {"tools", toolsArray}
        }}
    };
}

static std::string runSubprocessCapture(const std::string& binaryPath, const std::string& configFile, const std::string& fullCommand) {
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        return "%% Error: pipe creation failed";
    }

    pid_t pid = fork();
    if (pid == -1) {
        close(pipefd[0]);
        close(pipefd[1]);
        return "%% Error: fork failed";
    }

    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);

        execlp(binaryPath.c_str(), binaryPath.c_str(), "-C", configFile.c_str(), "-p", fullCommand.c_str(), nullptr);
        _exit(127);
    }

    close(pipefd[1]);
    std::string output;
    char buffer[512];
    ssize_t bytesRead;
    while ((bytesRead = read(pipefd[0], buffer, sizeof(buffer))) > 0) {
        output.append(buffer, bytesRead);
    }
    close(pipefd[0]);

    int status = 0;
    waitpid(pid, &status, 0);
    return output;
}

int runMcpServer(
    const std::vector<Command>& commands,
    cli::i18n::LocalizationManager& /*lang*/,
    const std::string& /*configDir*/,
    const std::string& configFile
) {
    std::vector<McpToolMapping> tools;
    collectTools(commands, "", "", tools);

    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;

        json req;
        try {
            req = json::parse(line);
        } catch (...) {
            continue;
        }

        std::string method = req.value("method", "");
        json id = req.contains("id") ? req["id"] : nullptr;

        if (method == "initialize") {
            json resp = {
                {"jsonrpc", "2.0"},
                {"id", id},
                {"result", {
                    {"protocolVersion", "2024-11-05"},
                    {"capabilities", {
                        {"tools", json::object()}
                    }},
                    {"serverInfo", {
                        {"name", "aethercli-mcp"},
                        {"version", "0.6.0"}
                    }}
                }}
            };
            std::cout << resp.dump() << "\n" << std::flush;
        }
        else if (method == "notifications/initialized") {
            continue;
        }
        else if (method == "tools/list") {
            json resp = buildToolsListResponse(tools, id);
            std::cout << resp.dump() << "\n" << std::flush;
        }
        else if (method == "tools/call") {
            std::string toolName = req["params"].value("name", "");
            json arguments = req["params"].value("arguments", json::object());

            const McpToolMapping* target = nullptr;
            for (const auto& t : tools) {
                if (t.toolName == toolName) {
                    target = &t;
                    break;
                }
            }

            if (!target) {
                json errResp = {
                    {"jsonrpc", "2.0"},
                    {"id", id},
                    {"error", {
                        {"code", -32601},
                        {"message", "Tool not found: " + toolName}
                    }}
                };
                std::cout << errResp.dump() << "\n" << std::flush;
                continue;
            }

            std::string fullCmd = target->commandPath;
            if (target->allowArgs && arguments.contains("args") && arguments["args"].is_string()) {
                fullCmd += " " + arguments["args"].get<std::string>();
            }

            std::string output = runSubprocessCapture("aethercli", configFile, fullCmd);

            json resp = {
                {"jsonrpc", "2.0"},
                {"id", id},
                {"result", {
                    {"content", json::array({
                        {
                            {"type", "text"},
                            {"text", output}
                        }
                    })},
                    {"isError", false}
                }}
            };
            std::cout << resp.dump() << "\n" << std::flush;
        }
        else {
            if (!id.is_null()) {
                json errResp = {
                    {"jsonrpc", "2.0"},
                    {"id", id},
                    {"error", {
                        {"code", -32601},
                        {"message", "Method not found: " + method}
                    }}
                };
                std::cout << errResp.dump() << "\n" << std::flush;
            }
        }
    }

    return 0;
}

} // namespace cli::mcp

#endif // AETHERCLI_ENABLE_MCP
