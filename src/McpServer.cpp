#ifdef AETHERCLI_ENABLE_MCP

#include "McpServer.hpp"
#include "CliUtils.hpp"
#include "CommandExecutor.hpp"
#include "Common.hpp"
#include "Logger.hpp"
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <set>
#include <nlohmann/json.hpp>

using json = nlohmann::json;
using namespace cli::parser;

extern std::string g_current_user;

namespace cli::mcp {

static const std::vector<std::string> SUPPORTED_VERSIONS = {
    "2026-07-28",
    "2025-11-25"
};
static const std::string SERVER_NAME = "aethercli-mcp";

// Limite quando o comando não declara max_output_bytes: a saída fica em memória.
static constexpr size_t DEFAULT_MAX_OUTPUT_BYTES = 10 * 1024 * 1024;
static constexpr size_t MAX_REQUEST_BYTES = 4 * 1024 * 1024;
static constexpr int MAX_JSON_DEPTH = 64;

// A cópia e a destruição de json são recursivas: aninhamento sem limite estoura
// a pilha do processo, e isso não é uma exceção que se possa capturar.
static bool exceedsJsonDepth(const std::string& text) {
    int depth = 0;
    bool in_string = false;
    bool escaped = false;
    for (char c : text) {
        if (in_string) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') in_string = false;
        } else if (c == '"') {
            in_string = true;
        } else if (c == '{' || c == '[') {
            if (++depth > MAX_JSON_DEPTH) return true;
        } else if (c == '}' || c == ']') {
            --depth;
        }
    }
    return false;
}

// Saída de comando pode não ser UTF-8 (binário, truncamento no meio de um
// caractere): com o handler padrão, dump() lançaria e derrubaria o servidor.
static void sendMessage(const json& message) {
    std::cout << message.dump(-1, ' ', false, json::error_handler_t::replace) << "\n" << std::flush;
}

static void sendError(const json& id, int code, const std::string& message) {
    sendMessage({
        {"jsonrpc", "2.0"},
        {"id", id},
        {"error", {
            {"code", code},
            {"message", message}
        }}
    });
}

struct McpToolMapping {
    std::string toolName;
    std::string commandPath;
    std::string description;
    bool allowArgs;
    std::string syntax;
    const Command* cmd;
};

static void collectTools(
    const std::vector<Command>& commands,
    const std::string& prefixName,
    const std::string& prefixPath,
    std::vector<McpToolMapping>& outTools
) {
    for (const auto& cmd : commands) {
        // A árvore de administração nunca é servida, para nenhum usuário: um agente
        // não pode alterar a própria árvore de comandos nem desligar o login.
        if (cmd.admin_only || (prefixPath.empty() && cmd.name == "configure")) {
            continue;
        }

        std::string currentName = prefixName.empty() ? cmd.name : prefixName + "_" + cmd.name;
        std::string currentPath = prefixPath.empty() ? cmd.name : prefixPath + " " + cmd.name;

        // Ativações internas dependem do shell interativo; aqui iriam para o sh.
        if (!cmd.activation.empty() && cmd.activation.rfind("internal:", 0) != 0) {
            std::string desc = cmd.short_desc.empty() ? cmd.description : cmd.short_desc;
            if (desc.empty()) {
                desc = "Execute: " + currentPath;
            }
            outTools.push_back({
                currentName,
                currentPath,
                desc,
                cmd.allow_args,
                cmd.syntax,
                &cmd
            });
        }

        if (!cmd.subcommands.empty()) {
            collectTools(cmd.subcommands, currentName, currentPath, outTools);
        }
    }
}

static json buildDiscoverResponse(const json& id) {
    json supported = json::array();
    for (const auto& v : SUPPORTED_VERSIONS) {
        supported.push_back(v);
    }

    return {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"result", {
            {"supportedVersions", supported},
            {"capabilities", {
                {"tools", {
                    {"listChanged", false}
                }}
            }},
            {"instructions", "aethercli management commands as MCP tools"},
            {"resultType", "complete"},
            {"ttlMs", 300000},
            {"cacheScope", "public"},
            {"_meta", {
                {"io.modelcontextprotocol/serverInfo", {
                    {"name", SERVER_NAME},
                    {"version", cli::VERSION}
                }}
            }}
        }}
    };
}

static json buildToolsListResponse(const std::vector<McpToolMapping>& tools, const json& id, bool isModern) {
    json toolsArray = json::array();

    for (const auto& tool : tools) {
        json toolObj;
        toolObj["name"] = tool.toolName;
        toolObj["description"] = tool.description;

        json annotations = json::object();
        annotations["title"] = tool.cmd->short_desc.empty() ? tool.cmd->name : tool.cmd->short_desc;
        if (tool.cmd->effect == "read") {
            annotations["readOnlyHint"] = true;
            annotations["destructiveHint"] = false;
        } else if (tool.cmd->effect == "destructive") {
            annotations["readOnlyHint"] = false;
            annotations["destructiveHint"] = true;
        } else { // "write"
            annotations["readOnlyHint"] = false;
            annotations["destructiveHint"] = false;
        }
        annotations["idempotentHint"] = tool.cmd->idempotent;
        annotations["openWorldHint"] = tool.cmd->open_world;
        toolObj["annotations"] = annotations;

        json inputSchema = json::object();
        inputSchema["type"] = "object";

        if (!tool.cmd->params.empty()) {
            json properties = json::object();
            json required = json::array();

            for (const auto& p : tool.cmd->params) {
                json pObj = json::object();
                pObj["type"] = p.type;
                if (!p.description.empty()) {
                    pObj["description"] = p.description;
                }
                if (!p.enum_values.empty()) {
                    pObj["enum"] = p.enum_values;
                }
                if (!p.pattern.empty()) {
                    pObj["pattern"] = p.pattern;
                }
                if (p.minimum.has_value()) {
                    pObj["minimum"] = p.minimum.value();
                }
                if (p.maximum.has_value()) {
                    pObj["maximum"] = p.maximum.value();
                }
                properties[p.name] = pObj;
                if (p.required) {
                    required.push_back(p.name);
                }
            }
            inputSchema["properties"] = properties;
            if (!required.empty()) {
                inputSchema["required"] = required;
            }
            inputSchema["additionalProperties"] = false;
        } else if (tool.allowArgs) {
            json properties = json::object();
            properties["args"] = {
                {"type", "string"},
                {"description", "Arguments for the command. Syntax: " + (tool.syntax.empty() ? tool.commandPath : tool.syntax)}
            };
            inputSchema["properties"] = properties;
        } else {
            // MCP-V: ferramenta sem parâmetros com {"type": "object", "additionalProperties": false}
            inputSchema["additionalProperties"] = false;
        }

        toolObj["inputSchema"] = inputSchema;
        toolsArray.push_back(toolObj);
    }

    json result = {
        {"tools", toolsArray}
    };

    if (isModern) {
        result["resultType"] = "complete";
        result["ttlMs"] = 300000;
        result["cacheScope"] = "public";
        result["_meta"] = {
            {"io.modelcontextprotocol/serverInfo", {
                {"name", SERVER_NAME},
                {"version", cli::VERSION}
            }}
        };
    }

    return {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"result", result}
    };
}

int runMcpServer(
    const std::vector<Command>& commands,
    cli::i18n::LocalizationManager& lang,
    const std::string& configDir,
    const std::string& /*configFile*/
) {
    std::vector<McpToolMapping> tools;
    collectTools(commands, "", "", tools);

    // "a_b" e "a b" geram o mesmo nome: o agente executaria a ferramenta errada.
    std::set<std::string> toolNames;
    for (const auto& t : tools) {
        if (!toolNames.insert(t.toolName).second) {
            std::string msg = lang.getOr("mcp_err_tool_collision", "%% Error: Duplicate MCP tool name: _VALUE_");
            const size_t marker = msg.find("_VALUE_");
            if (marker != std::string::npos) msg.replace(marker, 7, t.toolName);
            std::cerr << msg << std::endl;
            return 1;
        }
    }

    bool isLegacySession = false;

    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;

        if (line.size() > MAX_REQUEST_BYTES || exceedsJsonDepth(line)) {
            sendError(nullptr, -32600, "Invalid Request");
            continue;
        }

        json req;
        try {
            req = json::parse(line);
        } catch (...) {
            json parseErr = {
                {"jsonrpc", "2.0"},
                {"id", nullptr},
                {"error", {
                    {"code", -32700},
                    {"message", "Parse error"}
                }}
            };
            sendMessage(parseErr);
            continue;
        }

        if (!req.is_object()) {
            json invalidErr = {
                {"jsonrpc", "2.0"},
                {"id", nullptr},
                {"error", {
                    {"code", -32600},
                    {"message", "Invalid Request"}
                }}
            };
            sendMessage(invalidErr);
            continue;
        }

        json id = nullptr;
        if (req.contains("id") && (req["id"].is_string() || req["id"].is_number())) {
            id = req["id"];
        }
        const bool validVersion = req.contains("jsonrpc") && req["jsonrpc"] == "2.0";
        if (!validVersion || !req.contains("method") || !req["method"].is_string() ||
            (req.contains("id") && id.is_null())) {
            sendError(id, -32600, "Invalid Request");
            continue;
        }
        std::string method = req["method"].get<std::string>();

        // Sem "id" é notificação: nunca responde e não dispara execução.
        if (!req.contains("id")) continue;

        if (req.contains("params") && !req["params"].is_object()) {
            sendError(id, -32602, "Invalid params");
            continue;
        }

        // Verificação de protocolo via _meta (era moderna)
        bool hasVersionMeta = false;
        std::string metaVersion = "";
        if (req.contains("params") && req["params"].contains("_meta") && req["params"]["_meta"].is_object() &&
            req["params"]["_meta"].contains("io.modelcontextprotocol/protocolVersion") &&
            req["params"]["_meta"]["io.modelcontextprotocol/protocolVersion"].is_string()) {
            hasVersionMeta = true;
            metaVersion = req["params"]["_meta"]["io.modelcontextprotocol/protocolVersion"].get<std::string>();
        }

        if (hasVersionMeta) {
            bool isSupported = (std::find(SUPPORTED_VERSIONS.begin(), SUPPORTED_VERSIONS.end(), metaVersion) != SUPPORTED_VERSIONS.end());
            if (!isSupported) {
                json supportedArray = json::array();
                for (const auto& v : SUPPORTED_VERSIONS) supportedArray.push_back(v);

                json errResp = {
                    {"jsonrpc", "2.0"},
                    {"id", id},
                    {"error", {
                        {"code", -32022},
                        {"message", "Unsupported protocol version"},
                        {"data", {
                            {"supported", supportedArray},
                            {"requested", metaVersion}
                        }}
                    }}
                };
                sendMessage(errResp);
                continue;
            }
        }

        if (method == "server/discover") {
            json resp = buildDiscoverResponse(id);
            sendMessage(resp);
        }
        else if (method == "initialize") {
            isLegacySession = true;
            json resp = {
                {"jsonrpc", "2.0"},
                {"id", id},
                {"result", {
                    {"protocolVersion", "2025-11-25"},
                    {"capabilities", {
                        {"tools", json::object()}
                    }},
                    {"serverInfo", {
                        {"name", SERVER_NAME},
                        {"version", cli::VERSION}
                    }}
                }}
            };
            sendMessage(resp);
        }
        else if (method == "ping") {
            json result = json::object();
            if (hasVersionMeta || !isLegacySession) result["resultType"] = "complete";
            sendMessage({{"jsonrpc", "2.0"}, {"id", id}, {"result", result}});
        }
        else if (method == "tools/list") {
            bool isModern = hasVersionMeta || !isLegacySession;
            json resp = buildToolsListResponse(tools, id, isModern);
            sendMessage(resp);
        }
        else if (method == "tools/call") {
            std::string toolName = "";
            json arguments = json::object();
            if (req.contains("params")) {
                const json& callParams = req["params"];
                bool validParams = !callParams.contains("name") || callParams["name"].is_string();
                if (callParams.contains("arguments")) {
                    validParams = validParams && callParams["arguments"].is_object();
                }
                if (!validParams) {
                    sendError(id, -32602, "Invalid params");
                    continue;
                }
                toolName = callParams.value("name", "");
                if (callParams.contains("arguments")) arguments = callParams["arguments"];
            }

            const McpToolMapping* target = nullptr;
            for (const auto& t : tools) {
                if (t.toolName == toolName) {
                    target = &t;
                    break;
                }
            }

            if (!target) {
                sendError(id, -32602, "Unknown tool: " + toolName);
                continue;
            }

            std::string finalCmd = target->cmd->activation;
            bool argsError = false;
            std::string argsErrorMsg = "";
            std::map<std::string, std::string> param_env;

            if (!target->cmd->params.empty()) {
                std::vector<std::string> param_argv;
                auto val_err = validateJsonParams(target->cmd->params, arguments, target->cmd->name, param_env, param_argv, activationEndsOptions(target->cmd->activation));
                if (val_err.has_value()) {
                    argsError = true;
                    argsErrorMsg = formatParamError(val_err.value(), lang);
                } else {
                    for (const auto& a : param_argv) {
                        finalCmd += " " + cli::util::escape_shell_arg(a);
                    }
                }
            } else if (arguments.contains("args") && arguments["args"].is_string()) {
                std::string argsVal = arguments["args"].get<std::string>();
                if (!argsVal.empty()) {
                    if (!target->cmd->allow_args || argsVal.find('\0') != std::string::npos) {
                        argsError = true;
                        std::string errTpl = lang.getOr("cli_err_arguments", "% Command '_VALUE_' does not accept arguments.");
                        const size_t marker = errTpl.find("_VALUE_");
                        if (marker != std::string::npos) errTpl.replace(marker, 7, target->cmd->name);
                        argsErrorMsg = errTpl;
                    } else {
                        auto extraTokens = cli::util::split(argsVal);
                        if (!activationEndsOptions(finalCmd)) {
                            for (const auto& a : extraTokens) {
                                if (!a.empty() && a[0] == '-') {
                                    argsError = true;
                                    argsErrorMsg = lang.getOr("cli_err_flag_arguments", "%% Error: Security violation. Extra arguments starting with '-' are forbidden unless the command activation ends with ' --'.");
                                    break;
                                }
                            }
                        }
                        if (!argsError) {
                            for (const auto& a : extraTokens) {
                                finalCmd += " " + cli::util::escape_shell_arg(a);
                            }
                        }
                    }
                }
            } else if (!arguments.empty()) {
                argsError = true;
                std::string errTpl = lang.getOr("cli_err_arguments", "% Command '_VALUE_' does not accept arguments.");
                const size_t marker = errTpl.find("_VALUE_");
                if (marker != std::string::npos) errTpl.replace(marker, 7, target->cmd->name);
                argsErrorMsg = errTpl;
            }

            if (argsError) {
                bool isModern = hasVersionMeta || !isLegacySession;
                json resultObj = {
                    {"content", json::array({
                        {
                            {"type", "text"},
                            {"text", argsErrorMsg}
                        }
                    })},
                    {"isError", true}
                };
                if (isModern) {
                    resultObj["resultType"] = "complete";
                }
                json resp = {
                    {"jsonrpc", "2.0"},
                    {"id", id},
                    {"result", resultObj}
                };
                sendMessage(resp);
                continue;
            }

            cli::executor::ExecOptions opts;
            opts.capture_output = true;
            opts.redirect_stdin_null = true;
            opts.timeout_s = target->cmd->timeout_s;
            const size_t outputLimit = target->cmd->max_output_bytes > 0 ? target->cmd->max_output_bytes : DEFAULT_MAX_OUTPUT_BYTES;
            opts.max_output_bytes = outputLimit;
            if (!target->cmd->cwd.empty()) {
                opts.cwd = (target->cmd->cwd[0] == '/') ? target->cmd->cwd : configDir + "/" + target->cmd->cwd;
            }
            opts.env = target->cmd->env;
            for (const auto& [k, v] : param_env) {
                opts.env[k] = v;
            }

            cli::log::Logger::info(g_current_user + ": mcp: " + toolName);
            cli::executor::ExecResult r;
            try {
                r = cli::executor::CommandExecutor::executeShellCommand(finalCmd, opts);
            } catch (const std::exception& e) {
                sendError(id, -32603, e.what());
                continue;
            }

            std::string outputText = r.stdout_output;
            if (!r.stderr_output.empty()) {
                if (!outputText.empty() && outputText.back() != '\n') outputText += "\n";
                outputText += r.stderr_output;
            }

            if (r.output_truncated) {
                if (!outputText.empty() && outputText.back() != '\n') outputText += "\n";
                std::string truncMsg = lang.getOr("cli_msg_output_truncated", "% Output truncated: exceeded limit of _VALUE_ bytes.");
                const size_t marker = truncMsg.find("_VALUE_");
                if (marker != std::string::npos) truncMsg.replace(marker, 7, std::to_string(outputLimit));
                outputText += truncMsg + "\n";
            }

            bool isError = false;
            if (r.timed_out) {
                isError = true;
                if (!outputText.empty() && outputText.back() != '\n') outputText += "\n";
                std::string tMsg = lang.getOr("cli_msg_timeout", "% Command timed out after _VALUE_ seconds.");
                const size_t marker = tMsg.find("_VALUE_");
                if (marker != std::string::npos) tMsg.replace(marker, 7, std::to_string(target->cmd->timeout_s));
                outputText += tMsg;
            } else if (r.signaled) {
                isError = true;
                if (!outputText.empty() && outputText.back() != '\n') outputText += "\n";
                std::string sMsg = lang.getOr("cli_msg_signal", "% Command terminated by signal _VALUE_.");
                const size_t marker = sMsg.find("_VALUE_");
                if (marker != std::string::npos) sMsg.replace(marker, 7, std::to_string(r.term_signal));
                outputText += sMsg;
            } else if (r.exit_code != 0) {
                isError = true;
                if (!outputText.empty() && outputText.back() != '\n') outputText += "\n";
                if (r.exit_code == 127) {
                    outputText += lang.getOr("cli_err_command_not_found", "% Command not found or could not be executed (127).");
                } else {
                    std::string eMsg = lang.getOr("cli_msg_exit_status", "% Command exited with status _VALUE_.");
                    const size_t marker = eMsg.find("_VALUE_");
                    if (marker != std::string::npos) eMsg.replace(marker, 7, std::to_string(r.exit_code));
                    outputText += eMsg;
                }
            }

            json resultObj = {
                {"content", json::array({
                    {
                        {"type", "text"},
                        {"text", outputText}
                    }
                })},
                {"isError", isError}
            };

            bool isModern = hasVersionMeta || !isLegacySession;
            if (isModern) {
                resultObj["resultType"] = "complete";
                resultObj["_meta"] = {
                    {"io.modelcontextprotocol/serverInfo", {
                        {"name", SERVER_NAME},
                        {"version", cli::VERSION}
                    }}
                };
            }

            json resp = {
                {"jsonrpc", "2.0"},
                {"id", id},
                {"result", resultObj}
            };
            sendMessage(resp);
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
                sendMessage(errResp);
            }
        }
    }

    return 0;
}

} // namespace cli::mcp

#endif // AETHERCLI_ENABLE_MCP
