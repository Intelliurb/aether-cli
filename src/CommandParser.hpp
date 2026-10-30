/*
 * Copyright (c) 2026 caq@intelliurb.com
 * PROPRIEDADE DA INTELLIURB - SOMENTE USO INTERNO
 */
#pragma once
#include <string>
#include <vector>
#include <map>
#include <set>
#include <optional>
#include <fstream>
#include <iostream>
#include <regex>
#include <cctype>
#include <algorithm>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <climits>
#include "LocalizationManager.hpp"

namespace cli::parser {

struct CommandParam {
    std::string name;
    std::string type = "string"; // "string", "integer", "boolean"
    std::string description;
    bool required = false;
    std::vector<std::string> enum_values;
    std::string pattern;
    std::optional<long long> minimum;
    std::optional<long long> maximum;
    bool secret = false;
};

struct Command {
    std::string name;
    std::string abbrev;
    std::string short_desc;
    std::string description;
    std::string activation;
    std::string syntax;
    // Se true ("args": "append"), tokens extras do usuário são anexados ao
    // comando shell (escapados). Default false: comando não aceita argumentos.
    bool allow_args = false;
    bool admin_only = false;
    int timeout_s = 0;
    size_t max_output_bytes = 0;
    std::string cwd;
    std::map<std::string, std::string> env;
    std::vector<CommandParam> params;
    std::string effect = "write"; // "read", "write", "destructive"
    bool idempotent = false;
    bool open_world = true;
    bool is_async = false;
    int retention_s = 0;
    std::vector<Command> subcommands;
};

struct Config {
    std::vector<Command> commands;
    std::string motd;
    std::string config_version;
    // Autenticação opcional: se true, exige login contra <configDir>/users.json.
    bool require_authentication = false;
    bool restricted_session = false;
    int number_auth_fail = 3;
    std::string passwd_file;
    bool status_bar = false;
    std::string language = "en";
    std::string scripts_dir = "scripts";
};

class CommandParser {
public:
    static Config parseConfig(const std::string& filePath) {
        std::ifstream file(filePath);
        if (!file.is_open()) {
            throw std::runtime_error("Critical Error: Configuration file not found: " + filePath);
        }

        nlohmann::json j;
        try {
            file >> j;
        } catch (const nlohmann::json::parse_error& e) {
            throw std::runtime_error("Critical Error: Malformed JSON in " + filePath + " at byte " + std::to_string(e.byte) + ": " + e.what());
        }

        Config config;
        
        if (j.contains("scripts_dir") && j["scripts_dir"].is_string()) {
            config.scripts_dir = j["scripts_dir"];
        }

        if (j.contains("motd") && j["motd"].is_string()) {
            config.motd = j["motd"];
        }

        if (j.contains("config_version") && j["config_version"].is_string()) {
            config.config_version = j["config_version"];
        } else if (j.contains("version") && j["version"].is_string()) {
            config.config_version = j["version"];
        }

        auto parseBool = [&](const std::string& key, bool& out_val) {
            if (j.contains(key)) {
                const auto& v = j[key];
                if (v.is_boolean()) out_val = v.get<bool>();
                else if (v.is_string() && (v == "true" || v == "false")) out_val = (v == "true");
                else throw std::runtime_error("Critical Error: '" + key + "' must be \"true\" or \"false\" in " + filePath);
            }
        };

        parseBool("require_authentication", config.require_authentication);
        parseBool("restricted_session", config.restricted_session);

        if (j.contains("number_auth_fail")) {
            const auto& v = j["number_auth_fail"];
            int n = 0;
            if (v.is_number_integer()) n = v.get<int>();
            else if (v.is_string()) {
                try { n = std::stoi(v.get<std::string>()); } catch (...) { n = 0; }
            }
            if (n < 1) throw std::runtime_error("Critical Error: 'number_auth_fail' must be an integer >= 1 in " + filePath);
            config.number_auth_fail = n;
        }

        if (j.contains("passwd-file")) {
            const auto& v = j["passwd-file"];
            if (!v.is_string()) throw std::runtime_error("Critical Error: 'passwd-file' must be a string in " + filePath);
            config.passwd_file = v.get<std::string>();
        }

        parseBool("status_bar", config.status_bar);

        if (j.contains("language")) {
            const auto& v = j["language"];
            if (!v.is_string()) throw std::runtime_error("Critical Error: 'language' must be a string in " + filePath);
            config.language = v.get<std::string>();
        }

        if (!j.contains("commands")) {
            throw std::runtime_error("Critical Error: Missing mandatory top-level field 'commands' in " + filePath);
        }

        if (!j["commands"].is_array()) {
            throw std::runtime_error("Critical Error: Field 'commands' must be an array in " + filePath);
        }

        config.commands = parseCommands(j["commands"], "root");
        return config;
    }

private:
    static std::vector<Command> parseCommands(const nlohmann::json& j, const std::string& context) {
        std::vector<Command> commands;
        
        for (size_t i = 0; i < j.size(); ++i) {
            const auto& element = j[i];
            std::string current_context = context + ".commands[" + std::to_string(i) + "]";

            if (!element.is_object()) {
                throw std::runtime_error("Critical Error: Array element at " + current_context + " is not an object.");
            }

            // Validar campos obrigatórios
            if (!element.contains("name") || !element["name"].is_string() || element["name"].get<std::string>().empty()) {
                throw std::runtime_error("Critical Error: Missing or invalid mandatory field 'name' at " + current_context);
            }

            Command cmd;
            cmd.name = element["name"];
            
            // Campos opcionais com validação de tipo
            if (element.contains("abbrev")) {
                if (!element["abbrev"].is_string()) throw std::runtime_error("Error: 'abbrev' must be a string at " + current_context);
                cmd.abbrev = element["abbrev"];
            }

            if (element.contains("short_desc")) {
                if (!element["short_desc"].is_string()) throw std::runtime_error("Error: 'short_desc' must be a string at " + current_context);
                cmd.short_desc = element["short_desc"];
            }

            if (element.contains("description")) {
                if (!element["description"].is_string()) throw std::runtime_error("Error: 'description' must be a string at " + current_context);
                cmd.description = element["description"];
            }

            if (element.contains("activation")) {
                if (!element["activation"].is_string()) throw std::runtime_error("Error: 'activation' must be a string at " + current_context);
                cmd.activation = element["activation"];
            }

            if (element.contains("syntax")) {
                if (!element["syntax"].is_string()) throw std::runtime_error("Error: 'syntax' must be a string at " + current_context);
                cmd.syntax = element["syntax"];
            }

            if (element.contains("args")) {
                if (!element["args"].is_string()) throw std::runtime_error("Error: 'args' must be a string at " + current_context);
                std::string args_policy = element["args"];
                if (args_policy == "append") cmd.allow_args = true;
                else if (args_policy == "none") cmd.allow_args = false;
                else throw std::runtime_error("Error: 'args' must be \"none\" or \"append\" at " + current_context);
            }

            if (element.contains("admin_only")) {
                if (!element["admin_only"].is_boolean()) throw std::runtime_error("Error: 'admin_only' must be a boolean at " + current_context);
                cmd.admin_only = element["admin_only"].get<bool>();
            }

            if (element.contains("timeout_s")) {
                if (!element["timeout_s"].is_number_integer()) throw std::runtime_error("Error: 'timeout_s' must be an integer at " + current_context);
                int val = element["timeout_s"].get<int>();
                if (val < 0) throw std::runtime_error("Error: 'timeout_s' must be non-negative at " + current_context);
                cmd.timeout_s = val;
            }

            if (element.contains("max_output_bytes")) {
                if (!element["max_output_bytes"].is_number_integer() && !element["max_output_bytes"].is_number_unsigned()) {
                    throw std::runtime_error("Error: 'max_output_bytes' must be an integer at " + current_context);
                }
                long long val = element["max_output_bytes"].get<long long>();
                if (val < 0) throw std::runtime_error("Error: 'max_output_bytes' must be non-negative at " + current_context);
                cmd.max_output_bytes = static_cast<size_t>(val);
            }

            if (element.contains("cwd")) {
                if (!element["cwd"].is_string()) throw std::runtime_error("Error: 'cwd' must be a string at " + current_context);
                cmd.cwd = element["cwd"].get<std::string>();
            }

            if (element.contains("env")) {
                if (!element["env"].is_object()) throw std::runtime_error("Error: 'env' must be an object at " + current_context);
                for (auto it = element["env"].begin(); it != element["env"].end(); ++it) {
                    if (!it.value().is_string()) throw std::runtime_error("Error: 'env' values must be strings at " + current_context);
                    cmd.env[it.key()] = it.value().get<std::string>();
                }
            }

            if (element.contains("effect")) {
                if (!element["effect"].is_string()) throw std::runtime_error("Error: 'effect' must be a string at " + current_context);
                std::string eff = element["effect"].get<std::string>();
                if (eff != "read" && eff != "write" && eff != "destructive") {
                    throw std::runtime_error("Error: 'effect' must be 'read', 'write', or 'destructive' at " + current_context);
                }
                cmd.effect = eff;
            }

            if (element.contains("idempotent")) {
                if (!element["idempotent"].is_boolean()) throw std::runtime_error("Error: 'idempotent' must be a boolean at " + current_context);
                cmd.idempotent = element["idempotent"].get<bool>();
            }

            if (element.contains("open_world")) {
                if (!element["open_world"].is_boolean()) throw std::runtime_error("Error: 'open_world' must be a boolean at " + current_context);
                cmd.open_world = element["open_world"].get<bool>();
            }

            if (element.contains("async")) {
                if (!element["async"].is_boolean()) throw std::runtime_error("Error: 'async' must be a boolean at " + current_context);
                cmd.is_async = element["async"].get<bool>();
            }

            if (element.contains("retention_s")) {
                if (!element["retention_s"].is_number_integer()) throw std::runtime_error("Error: 'retention_s' must be an integer at " + current_context);
                int r_val = element["retention_s"].get<int>();
                if (r_val < 0) throw std::runtime_error("Error: 'retention_s' must be non-negative at " + current_context);
                cmd.retention_s = r_val;
            }

            if (element.contains("params")) {
                if (!element["params"].is_array()) {
                    throw std::runtime_error("Error: 'params' must be an array at " + current_context);
                }
                std::set<std::string> seen_param_names;
                for (size_t p_idx = 0; p_idx < element["params"].size(); ++p_idx) {
                    const auto& p_elem = element["params"][p_idx];
                    std::string p_ctx = current_context + ".params[" + std::to_string(p_idx) + "]";
                    if (!p_elem.is_object()) {
                        throw std::runtime_error("Error: Parameter at " + p_ctx + " must be an object.");
                    }
                    if (!p_elem.contains("name") || !p_elem["name"].is_string() || p_elem["name"].get<std::string>().empty()) {
                        throw std::runtime_error("Error: Missing or invalid mandatory field 'name' at " + p_ctx);
                    }
                    std::string p_name = p_elem["name"].get<std::string>();
                    // O nome vira AETHERCLI_PARAM_<NOME>: precisa ser identificador de shell.
                    for (size_t c_idx = 0; c_idx < p_name.size(); ++c_idx) {
                        const unsigned char ch = static_cast<unsigned char>(p_name[c_idx]);
                        if (!(isalpha(ch) || ch == '_' || (c_idx > 0 && isdigit(ch)))) {
                            throw std::runtime_error("Error: Invalid parameter name '" + p_name + "' at " + p_ctx);
                        }
                    }
                    if (seen_param_names.find(p_name) != seen_param_names.end()) {
                        throw std::runtime_error("Error: Duplicate parameter name '" + p_name + "' at " + p_ctx);
                    }
                    seen_param_names.insert(p_name);

                    CommandParam param;
                    param.name = p_name;

                    if (p_elem.contains("type")) {
                        if (!p_elem["type"].is_string()) {
                            throw std::runtime_error("Error: 'type' must be a string at " + p_ctx);
                        }
                        std::string t_str = p_elem["type"].get<std::string>();
                        if (t_str != "string" && t_str != "integer" && t_str != "boolean") {
                            throw std::runtime_error("Error: Unknown parameter type '" + t_str + "' at " + p_ctx);
                        }
                        param.type = t_str;
                    }

                    if (p_elem.contains("description")) {
                        if (!p_elem["description"].is_string()) {
                            throw std::runtime_error("Error: 'description' must be a string at " + p_ctx);
                        }
                        param.description = p_elem["description"].get<std::string>();
                    }

                    if (p_elem.contains("required")) {
                        if (!p_elem["required"].is_boolean()) {
                            throw std::runtime_error("Error: 'required' must be a boolean at " + p_ctx);
                        }
                        param.required = p_elem["required"].get<bool>();
                    }

                    if (p_elem.contains("enum")) {
                        if (!p_elem["enum"].is_array()) {
                            throw std::runtime_error("Error: 'enum' must be an array at " + p_ctx);
                        }
                        if (p_elem["enum"].empty()) {
                            throw std::runtime_error("Error: 'enum' cannot be empty at " + p_ctx);
                        }
                        std::set<std::string> seen_enum;
                        for (const auto& ev : p_elem["enum"]) {
                            if (!ev.is_string()) {
                                throw std::runtime_error("Error: 'enum' elements must be strings at " + p_ctx);
                            }
                            std::string ev_str = ev.get<std::string>();
                            if (seen_enum.find(ev_str) != seen_enum.end()) {
                                throw std::runtime_error("Error: Duplicate value in 'enum' at " + p_ctx);
                            }
                            seen_enum.insert(ev_str);
                            param.enum_values.push_back(ev_str);
                        }
                    }

                    if (p_elem.contains("pattern")) {
                        if (!p_elem["pattern"].is_string()) {
                            throw std::runtime_error("Error: 'pattern' must be a string at " + p_ctx);
                        }
                        param.pattern = p_elem["pattern"].get<std::string>();
                        try {
                            std::regex validated(param.pattern);
                        } catch (const std::regex_error&) {
                            throw std::runtime_error("Error: Invalid 'pattern' at " + p_ctx);
                        }
                    }

                    if (p_elem.contains("minimum")) {
                        if (!p_elem["minimum"].is_number_integer()) {
                            throw std::runtime_error("Error: 'minimum' must be an integer at " + p_ctx);
                        }
                        param.minimum = p_elem["minimum"].get<long long>();
                    }

                    if (p_elem.contains("maximum")) {
                        if (!p_elem["maximum"].is_number_integer()) {
                            throw std::runtime_error("Error: 'maximum' must be an integer at " + p_ctx);
                        }
                        param.maximum = p_elem["maximum"].get<long long>();
                    }

                    if (param.minimum.has_value() && param.maximum.has_value() && param.minimum.value() > param.maximum.value()) {
                        throw std::runtime_error("Error: 'minimum' cannot be greater than 'maximum' at " + p_ctx);
                    }

                    if (p_elem.contains("secret")) {
                        if (!p_elem["secret"].is_boolean()) {
                            throw std::runtime_error("Error: 'secret' must be a boolean at " + p_ctx);
                        }
                        param.secret = p_elem["secret"].get<bool>();
                    }

                    cmd.params.push_back(param);
                }
            }

            if (cmd.allow_args && !cmd.params.empty()) {
                throw std::runtime_error("Error: 'params' and 'args': 'append' are mutually exclusive at " + current_context);
            }

            if (element.contains("subcommands")) {
                if (!element["subcommands"].is_array()) {
                    throw std::runtime_error("Critical Error: 'subcommands' must be an array at " + current_context);
                }
                cmd.subcommands = parseCommands(element["subcommands"], current_context + "(" + cmd.name + ")");
            }

            commands.push_back(cmd);
        }
        return commands;
    }
};

struct ParamValidationError {
    std::string error_key;
    std::string param_name;
    std::string value;
    std::string detail;
};

inline std::string formatParamError(const ParamValidationError& err, const cli::i18n::LocalizationManager& lang) {
    std::string fallback = "%% Error: Parameter validation failed";
    if (err.error_key == "cli_err_param_required") fallback = "%% Error: Parameter '_VALUE_' is required.";
    else if (err.error_key == "cli_err_param_type") fallback = "%% Error: Parameter '_VALUE_' must be of type _TYPE_.";
    else if (err.error_key == "cli_err_param_enum") fallback = "%% Error: Value for parameter '_VALUE_' must be one of: _ENUM_.";
    else if (err.error_key == "cli_err_param_pattern") fallback = "%% Error: Parameter '_VALUE_' does not match pattern '_PATTERN_'.";
    else if (err.error_key == "cli_err_param_range") fallback = "%% Error: Parameter '_VALUE_' is out of range.";
    else if (err.error_key == "cli_err_param_unknown") fallback = "%% Error: Unknown parameter '_VALUE_'.";
    else if (err.error_key == "cli_err_too_many_params") fallback = "%% Error: Too many parameters for command '_VALUE_'.";
    else if (err.error_key == "cli_err_flag_arguments") fallback = "%% Error: Security violation. Extra arguments starting with '-' are forbidden unless the command activation ends with ' --'.";

    std::string msg = lang.getOr(err.error_key, fallback);
    size_t m_val = msg.find("_VALUE_");
    if (m_val != std::string::npos) msg.replace(m_val, 7, err.param_name);
    size_t m_type = msg.find("_TYPE_");
    if (m_type != std::string::npos) msg.replace(m_type, 6, err.detail);
    size_t m_enum = msg.find("_ENUM_");
    if (m_enum != std::string::npos) msg.replace(m_enum, 6, err.detail);
    size_t m_pat = msg.find("_PATTERN_");
    if (m_pat != std::string::npos) msg.replace(m_pat, 9, err.detail);
    return msg;
}

// Um demarcador " --" no fim da ativação declara que valores com '-' são dados.
inline bool activationEndsOptions(const std::string& activation) {
    return activation.length() >= 3 && activation.compare(activation.length() - 3, 3, " --") == 0;
}

// Falha fechada: padrão inválido ou erro do motor de regex recusa o valor.
inline bool matchesPattern(const std::string& pattern, const std::string& value) {
    try {
        return std::regex_match(value, std::regex(pattern));
    } catch (const std::regex_error&) {
        return false;
    }
}

// Valor livre com '-' (inclusive inteiro negativo) viraria opção do programa
// chamado; enum é valor declarado.
inline bool isForbiddenFlagValue(const CommandParam& p, const std::string& value, bool allow_flags) {
    return !allow_flags && p.enum_values.empty() && !value.empty() && value[0] == '-';
}

inline std::optional<ParamValidationError> validatePositionalParams(
    const std::vector<CommandParam>& params,
    const std::vector<std::string>& extra_tokens,
    const std::string& cmd_name,
    std::map<std::string, std::string>& out_env,
    std::vector<std::string>& out_argv,
    bool allow_flags = false
) {
    if (extra_tokens.size() > params.size()) {
        return ParamValidationError{"cli_err_too_many_params", cmd_name, "", ""};
    }

    for (size_t i = 0; i < params.size(); ++i) {
        const auto& p = params[i];
        if (i < extra_tokens.size()) {
            const std::string& val = extra_tokens[i];
            if (p.type == "string") {
                if (!p.enum_values.empty()) {
                    if (std::find(p.enum_values.begin(), p.enum_values.end(), val) == p.enum_values.end()) {
                        std::string enum_str;
                        for (size_t k = 0; k < p.enum_values.size(); ++k) {
                            if (k > 0) enum_str += ", ";
                            enum_str += p.enum_values[k];
                        }
                        return ParamValidationError{"cli_err_param_enum", p.name, val, enum_str};
                    }
                }
                if (!p.pattern.empty() && !matchesPattern(p.pattern, val)) {
                    return ParamValidationError{"cli_err_param_pattern", p.name, val, p.pattern};
                }
                if (isForbiddenFlagValue(p, val, allow_flags)) {
                    return ParamValidationError{"cli_err_flag_arguments", p.name, val, ""};
                }
            } else if (p.type == "integer") {
                if (val.empty()) {
                    return ParamValidationError{"cli_err_param_type", p.name, val, "integer"};
                }
                size_t start = 0;
                if (val[0] == '-' || val[0] == '+') start = 1;
                if (start == val.size()) {
                    return ParamValidationError{"cli_err_param_type", p.name, val, "integer"};
                }
                for (size_t c_idx = start; c_idx < val.size(); ++c_idx) {
                    if (!isdigit(static_cast<unsigned char>(val[c_idx]))) {
                        return ParamValidationError{"cli_err_param_type", p.name, val, "integer"};
                    }
                }
                long long num = 0;
                try {
                    num = std::stoll(val);
                } catch (...) {
                    return ParamValidationError{"cli_err_param_type", p.name, val, "integer"};
                }
                if (p.minimum.has_value() && num < p.minimum.value()) {
                    return ParamValidationError{"cli_err_param_range", p.name, val, ""};
                }
                if (p.maximum.has_value() && num > p.maximum.value()) {
                    return ParamValidationError{"cli_err_param_range", p.name, val, ""};
                }
                if (isForbiddenFlagValue(p, val, allow_flags)) {
                    return ParamValidationError{"cli_err_flag_arguments", p.name, val, ""};
                }
                if (!p.enum_values.empty()) {
                    if (std::find(p.enum_values.begin(), p.enum_values.end(), val) == p.enum_values.end()) {
                        std::string enum_str;
                        for (size_t k = 0; k < p.enum_values.size(); ++k) {
                            if (k > 0) enum_str += ", ";
                            enum_str += p.enum_values[k];
                        }
                        return ParamValidationError{"cli_err_param_enum", p.name, val, enum_str};
                    }
                }
            } else if (p.type == "boolean") {
                if (val != "true" && val != "false") {
                    return ParamValidationError{"cli_err_param_type", p.name, val, "boolean"};
                }
            }

            out_argv.push_back(val);
            std::string upper_name = p.name;
            for (char& c : upper_name) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
            out_env["AETHERCLI_PARAM_" + upper_name] = val;
        } else {
            if (p.required) {
                return ParamValidationError{"cli_err_param_required", p.name, "", ""};
            }
        }
    }
    return std::nullopt;
}

inline std::optional<ParamValidationError> validateJsonParams(
    const std::vector<CommandParam>& params,
    const nlohmann::json& args,
    const std::string& cmd_name,
    std::map<std::string, std::string>& out_env,
    std::vector<std::string>& out_argv,
    bool allow_flags = false
) {
    if (!args.is_object()) {
        return ParamValidationError{"cli_err_param_type", cmd_name, "", "object"};
    }

    for (auto it = args.begin(); it != args.end(); ++it) {
        bool found = false;
        for (const auto& p : params) {
            if (p.name == it.key()) { found = true; break; }
        }
        if (!found) {
            return ParamValidationError{"cli_err_too_many_params", it.key(), "", ""};
        }
    }

    // Opcional omitido antes de um valor presente vira "": o argv é posicional e
    // o valor seguinte não pode cair na posição de outro parâmetro.
    size_t pending_empty = 0;
    for (const auto& p : params) {
        if (args.contains(p.name)) {
            const auto& jval = args[p.name];
            std::string val_str;

            if (p.type == "string") {
                if (!jval.is_string()) {
                    return ParamValidationError{"cli_err_param_type", p.name, jval.dump(), "string"};
                }
                val_str = jval.get<std::string>();
                if (!p.enum_values.empty()) {
                    if (std::find(p.enum_values.begin(), p.enum_values.end(), val_str) == p.enum_values.end()) {
                        std::string enum_str;
                        for (size_t k = 0; k < p.enum_values.size(); ++k) {
                            if (k > 0) enum_str += ", ";
                            enum_str += p.enum_values[k];
                        }
                        return ParamValidationError{"cli_err_param_enum", p.name, val_str, enum_str};
                    }
                }
                if (!p.pattern.empty() && !matchesPattern(p.pattern, val_str)) {
                    return ParamValidationError{"cli_err_param_pattern", p.name, val_str, p.pattern};
                }
                if (isForbiddenFlagValue(p, val_str, allow_flags)) {
                    return ParamValidationError{"cli_err_flag_arguments", p.name, val_str, ""};
                }
                // NUL cortaria o valor em c_str(), depois de validado inteiro.
                if (val_str.find('\0') != std::string::npos) {
                    return ParamValidationError{"cli_err_param_type", p.name, "", "string"};
                }
            } else if (p.type == "integer") {
                if (!jval.is_number_integer() ||
                    (jval.is_number_unsigned() && jval.get<unsigned long long>() > static_cast<unsigned long long>(LLONG_MAX))) {
                    return ParamValidationError{"cli_err_param_type", p.name, jval.dump(), "integer"};
                }
                long long num = jval.get<long long>();
                val_str = std::to_string(num);
                if (p.minimum.has_value() && num < p.minimum.value()) {
                    return ParamValidationError{"cli_err_param_range", p.name, val_str, ""};
                }
                if (p.maximum.has_value() && num > p.maximum.value()) {
                    return ParamValidationError{"cli_err_param_range", p.name, val_str, ""};
                }
                if (isForbiddenFlagValue(p, val_str, allow_flags)) {
                    return ParamValidationError{"cli_err_flag_arguments", p.name, val_str, ""};
                }
                if (!p.enum_values.empty()) {
                    if (std::find(p.enum_values.begin(), p.enum_values.end(), val_str) == p.enum_values.end()) {
                        std::string enum_str;
                        for (size_t k = 0; k < p.enum_values.size(); ++k) {
                            if (k > 0) enum_str += ", ";
                            enum_str += p.enum_values[k];
                        }
                        return ParamValidationError{"cli_err_param_enum", p.name, val_str, enum_str};
                    }
                }
            } else if (p.type == "boolean") {
                if (!jval.is_boolean()) {
                    return ParamValidationError{"cli_err_param_type", p.name, jval.dump(), "boolean"};
                }
                val_str = jval.get<bool>() ? "true" : "false";
            }

            out_argv.insert(out_argv.end(), pending_empty, "");
            pending_empty = 0;
            out_argv.push_back(val_str);
            std::string upper_name = p.name;
            for (char& c : upper_name) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));
            out_env["AETHERCLI_PARAM_" + upper_name] = val_str;
        } else {
            if (p.required) {
                return ParamValidationError{"cli_err_param_required", p.name, "", ""};
            }
            ++pending_empty;
        }
    }
    return std::nullopt;
}

} // namespace cli::parser
