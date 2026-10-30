/*
 * Copyright (c) 2026 caq@intelliurb.com
 */
#pragma once
#include <string>
#include <fstream>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <mutex>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdlib>
#include <nlohmann/json.hpp>

namespace cli::log {

struct AuditRecord {
    std::string user;
    std::string caller; // "interactive", "headless", "mcp"
    std::string client_name;
    std::string command;
    nlohmann::json params = nlohmann::json::object();
    int status = 0;
    long long duration_ms = 0;
};

class AuditLogger {
public:
    static void setAuditPath(const std::string& path) {
        std::lock_guard<std::mutex> lock(audit_mutex);
        audit_path_override = path;
    }

    static std::string getAuditPath() {
        std::lock_guard<std::mutex> lock(audit_mutex);
        if (!audit_path_override.empty()) return audit_path_override;
        const char* home = std::getenv("HOME");
        // Sem HOME não há destino privado: /tmp seria previsível e compartilhado.
        return home ? (std::string(home) + "/.aethercli/audit.jsonl") : "";
    }

    static void log(const AuditRecord& rec) {
        std::lock_guard<std::mutex> lock(audit_mutex);

        std::string path = audit_path_override;
        if (path.empty()) {
            const char* home = std::getenv("HOME");
            if (!home) return;
            std::string dir = std::string(home) + "/.aethercli";
            mkdir(dir.c_str(), 0700);
            chmod(dir.c_str(), 0700);
            path = dir + "/audit.jsonl";
        } else {
            // mkdir não altera um diretório existente; chmod aqui mudaria o modo
            // de um diretório arbitrário escolhido pelo chamador.
            size_t slash = path.rfind('/');
            if (slash != std::string::npos) {
                mkdir(path.substr(0, slash).c_str(), 0700);
            }
        }

        auto now = std::chrono::system_clock::now();
        std::time_t t = std::chrono::system_clock::to_time_t(now);

        char buf[64];
        std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&t));

        nlohmann::json j;
        j["timestamp"] = static_cast<long long>(t);
        j["iso_time"] = std::string(buf);
        j["user"] = rec.user.empty() ? "-" : rec.user;
        j["caller"] = rec.caller;
        if (!rec.client_name.empty()) {
            j["client_name"] = rec.client_name;
        }
        j["command"] = rec.command;
        j["params"] = rec.params;
        j["status"] = rec.status;
        j["duration_ms"] = rec.duration_ms;

        std::string line = j.dump() + "\n";

        int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0600);
        if (fd != -1) {
            fchmod(fd, 0600);
            ssize_t written = 0;
            while (written < static_cast<ssize_t>(line.size())) {
                ssize_t n = write(fd, line.data() + written, line.size() - written);
                if (n <= 0) break;
                written += n;
            }
            close(fd);
        }
    }

private:
    static inline std::string audit_path_override = "";
    static inline std::mutex audit_mutex;
};


class Logger {
public:
    static void setLogPath(const std::string& path) {
        std::lock_guard<std::mutex> lock(log_mutex);
        log_path = path;
    }

    static void setEnabled(bool enabled) {
        std::lock_guard<std::mutex> lock(log_mutex);
        log_enabled = enabled;
    }

    // Linhas no formato syslog: "Mmm dd hh:mm:ss <facility> <severity>: mensagem".
    // Facility fixa "user" (aplicação de nível de usuário).
    static void info(const std::string& message) { write("info", message); }
    static void error(const std::string& message) { write("error", message); }

private:
    static void write(const char* severity, const std::string& message) {
        std::lock_guard<std::mutex> lock(log_mutex);

        if (!log_enabled || log_path.empty()) return;

        // O log contém argumentos de comandos: nasce legível só pelo dono.
        int fd = open(log_path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0600);
        if (fd == -1) return;
        struct stat st {};
        if (fstat(fd, &st) == 0 && S_ISREG(st.st_mode)) fchmod(fd, 0600);
        close(fd);

        std::ofstream file(log_path, std::ios::app);
        if (!file.is_open()) return;

        auto now = std::chrono::system_clock::now();
        std::time_t t = std::chrono::system_clock::to_time_t(now);

        file << std::put_time(std::localtime(&t), "%b %e %H:%M:%S")
             << " user " << severity << ": " << message << std::endl;
    }

    static inline std::string log_path = "";
    static inline bool log_enabled = false;
    static inline std::mutex log_mutex;
};

} // namespace cli::log
