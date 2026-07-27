/*
 * Copyright (c) 2026 caq@intelliurb.com
 */
#pragma once
#include <algorithm>
#include <cctype>
#include <string>
#include <vector>
#include <cstdlib>
#if defined(AETHERCLI_AI_OLLAMA) || defined(AETHERCLI_AI_LLAMACPP)
#include <nlohmann/json.hpp>
#include <httplib.h>
#endif
#include "CliUtils.hpp"
#include "CommandParser.hpp"

namespace cli::ai {

inline constexpr const char* AI_REMOTE_HOST_BLOCKED = "__AETHERCLI_AI_REMOTE_HOST_BLOCKED__";

inline std::string getEnvOrDefault(const std::string& var, const std::string& default_val) {
    const char* value = std::getenv(var.c_str());
    return value ? std::string(value) : default_val;
}

inline bool isLocalAIHost(std::string host) {
    std::transform(host.begin(), host.end(), host.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return host == "localhost" || host == "127.0.0.1" || host == "::1" || host == "[::1]";
}

#if defined(AETHERCLI_AI_OLLAMA) || defined(AETHERCLI_AI_LLAMACPP)
inline int getEnvPortOrDefault(const std::string& var, int default_val) {
    const char* value = std::getenv(var.c_str());
    if (!value) return default_val;
    try {
        return std::stoi(value);
    } catch (...) {
        return default_val;
    }
}
#endif

#if defined(AETHERCLI_AI_APPLEINTELLIGENCE)
inline constexpr const char* APPLE_AI_UNAVAILABLE = "__AETHERCLI_APPLE_AI_UNAVAILABLE__";
inline constexpr const char* APPLE_AI_ERROR = "__AETHERCLI_APPLE_AI_ERROR__";
inline constexpr const char* APPLE_AI_TIMEOUT = "__AETHERCLI_APPLE_AI_TIMEOUT__";
extern "C" char* aethercli_apple_intelligence_ask(const char* instructions, const char* prompt);
extern "C" void aethercli_apple_intelligence_free(char* response);
#endif

// Fala a API OpenAI-compatible (/v1/chat/completions) do backend selecionado.
class AIIntegration {
public:
    AIIntegration()
#if defined(AETHERCLI_AI_OLLAMA)
        : host(getEnvOrDefault("AI_HOST", "localhost")), port(getEnvPortOrDefault("AI_PORT", 11434)),
          model(getEnvOrDefault("AI_MODEL", "default")), api_key(getEnvOrDefault("AI_API_KEY", "")) {}
#elif defined(AETHERCLI_AI_LLAMACPP)
        : host(getEnvOrDefault("AI_HOST", "localhost")), port(getEnvPortOrDefault("AI_PORT", 8080)),
          model(getEnvOrDefault("AI_MODEL", "default")), api_key(getEnvOrDefault("AI_API_KEY", "")) {}
#else
        {}
#endif

#if defined(AETHERCLI_AI_OLLAMA) || defined(AETHERCLI_AI_LLAMACPP)
    AIIntegration(const std::string& h, int p, const std::string& m, const std::string& key = "")
        : host(h), port(p), model(m), api_key(key) {}
#endif

    std::string ask(const std::string& prompt, const std::vector<cli::parser::Command>& availableCommands, const std::string& systemInstruction) {
        std::string commandList;
        appendCommandList(availableCommands, "", commandList);

#if defined(AETHERCLI_AI_APPLEINTELLIGENCE)
        char* response = aethercli_apple_intelligence_ask((systemInstruction + "\nComandos disponíveis:\n" + commandList).c_str(),
                                                           prompt.c_str());
        if (!response) return APPLE_AI_ERROR;
        std::string answer(response);
        aethercli_apple_intelligence_free(response);
        return cli::util::stripReasoning(answer);
#else
        if (!isLocalAIHost(host)) return AI_REMOTE_HOST_BLOCKED;
        nlohmann::json body = {
            {"model", model},
            {"messages", {
                {{"role", "system"}, {"content", systemInstruction + "\nComandos disponíveis:\n" + commandList}},
                {{"role", "user"}, {"content", prompt}}
            }},
            {"temperature", 0.2},
            {"stream", false}
        };

        std::string lastError;
        try {
                httplib::Client cli(host, port);
                cli.set_connection_timeout(5, 0);
                cli.set_read_timeout(60, 0); // modelos locais pequenos podem demorar
                if (!api_key.empty()) {
                    cli.set_default_headers({{"Authorization", "Bearer " + api_key}});
                }

                auto res = cli.Post("/v1/chat/completions", body.dump(), "application/json");
                if (!res) {
                    lastError = "% Erro: Serviço AI não disponível em " + host + ":" + std::to_string(port);
                    return lastError;
                }
                if (res->status != 200) {
                    lastError = "% Erro: Serviço AI em " + host + ":" + std::to_string(port) +
                                " retornou status " + std::to_string(res->status);
                    return lastError;
                }

                try {
                    auto j = nlohmann::json::parse(res->body);
                    if (!j.contains("choices") || !j["choices"].is_array() || j["choices"].empty() ||
                        !j["choices"][0].contains("message") ||
                        !j["choices"][0]["message"].contains("content") ||
                        !j["choices"][0]["message"]["content"].is_string()) {
                        return "% Erro: Resposta do AI em formato inválido";
                    }
                    return cli::util::stripReasoning(j["choices"][0]["message"]["content"].get<std::string>());
                } catch (const nlohmann::json::exception& e) {
                    return "% Erro ao fazer parse da resposta do AI: " + std::string(e.what());
                }
        } catch (const std::exception& e) {
            lastError = "% Erro ao conectar ao serviço AI: " + std::string(e.what());
        }
        return lastError;
#endif
    }

private:
    // Lista recursivamente os comandos executáveis (folhas com activation),
    // com o caminho completo, para o system prompt.
    static void appendCommandList(const std::vector<cli::parser::Command>& cmds, const std::string& prefix, std::string& out) {
        for (const auto& c : cmds) {
            std::string path = prefix.empty() ? c.name : prefix + " " + c.name;
            if (!c.activation.empty() && c.activation.rfind("internal:", 0) != 0) {
                out += "- " + path;
                const std::string& desc = !c.description.empty() ? c.description : c.short_desc;
                if (!desc.empty()) out += ": " + desc;
                out += "\n";
            }
            appendCommandList(c.subcommands, path, out);
        }
    }

#if defined(AETHERCLI_AI_OLLAMA) || defined(AETHERCLI_AI_LLAMACPP)
    std::string host;
    int port;
    std::string model;
    std::string api_key;
#endif
};

} // namespace cli::ai
