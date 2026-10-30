/*
 * Copyright (c) 2026 caq@intelliurb.com
 */
#pragma once
#include <string>
#include <vector>
#include <map>
#include <stdexcept>
#include <cerrno>
#include <csignal>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <poll.h>
#include <chrono>
#include <algorithm>

namespace cli::executor {

inline volatile sig_atomic_t g_active_child_pid = 0;
inline volatile sig_atomic_t g_pending_exit_signal = 0;

inline void forward_signal_to_child(int sig) {
    // SIGTERM/SIGHUP encerram o aethercli: o filho está em outro grupo e não os
    // recebe sozinho, então o pai repassa e termina depois de colhê-lo.
    if (sig == SIGTERM || sig == SIGHUP) g_pending_exit_signal = sig;
    pid_t cp = g_active_child_pid;
    if (cp > 0) {
        kill(-cp, sig);
    }
}

struct ExecOptions {
    int timeout_s = 0;              // 0 = no timeout
    size_t max_output_bytes = 0;    // 0 = unlimited
    std::string cwd;
    std::map<std::string, std::string> env;
    bool capture_output = false;
    bool redirect_stdin_null = false;
};

struct ExecResult {
    int exit_code = 0;      // válido quando !signaled e !timed_out
    bool signaled = false;
    int term_signal = 0;
    bool timed_out = false;
    bool output_truncated = false;
    std::string stdout_output;
    std::string stderr_output;
};

class CommandExecutor {
public:
    static ExecResult execute(const std::vector<std::string>& args, const ExecOptions& options = {}) {
        if (args.empty()) {
            throw std::runtime_error("Empty command arguments!");
        }

        // Ctrl+C deve interromper apenas o comando filho, não o shell: o pai
        // encaminha SIGINT/SIGQUIT para o grupo de processos do filho.
        // Grupo próprio só quando é preciso sinalizar a árvore do comando (timeout)
        // ou isolá-la do terminal (MCP). Fora disso o filho fica no grupo do shell:
        // recebe os sinais do tty e convive com pipelines (aethercli -p x | less).
        const bool own_group = options.timeout_s > 0 || options.redirect_stdin_null;

        struct sigaction sa_fwd {}, old_int, old_quit, old_term {}, old_hup {};
        sa_fwd.sa_handler = forward_signal_to_child;
        sigaction(SIGINT, &sa_fwd, &old_int);
        sigaction(SIGQUIT, &sa_fwd, &old_quit);
        // Sinal ignorado na herança (nohup) continua ignorado, no pai e no filho.
        auto install_exit_forward = [&](int sig, struct sigaction& old) -> bool {
            if (!own_group || sigaction(sig, nullptr, &old) != 0 || old.sa_handler == SIG_IGN) return false;
            return sigaction(sig, &sa_fwd, nullptr) == 0;
        };
        const bool forward_term = install_exit_forward(SIGTERM, old_term);
        const bool forward_hup = install_exit_forward(SIGHUP, old_hup);

        // O filho ganha grupo próprio; se pode usar o terminal (mesmo com stdin
        // redirecionado, como sudo pedindo senha em /dev/tty), esse grupo precisa
        // ser o foreground, senão o tty o para com SIGTTIN/SIGTTOU.
        int tty_fd = -1;
        if (own_group && !options.redirect_stdin_null) {
            tty_fd = open("/dev/tty", O_RDWR | O_NOCTTY | O_CLOEXEC);
            if (tty_fd >= 0 && tcgetpgrp(tty_fd) != getpgrp()) {
                close(tty_fd);
                tty_fd = -1;
            }
        }
        const bool give_tty = tty_fd >= 0;
        struct sigaction sa_ign {}, old_ttou;
        sa_ign.sa_handler = SIG_IGN;
        if (give_tty) sigaction(SIGTTOU, &sa_ign, &old_ttou);
        auto restore_signals = [&]() {
            if (give_tty) {
                sigaction(SIGTTOU, &old_ttou, nullptr);
                close(tty_fd);
            }
            sigaction(SIGINT, &old_int, nullptr);
            sigaction(SIGQUIT, &old_quit, nullptr);
            if (forward_term) sigaction(SIGTERM, &old_term, nullptr);
            if (forward_hup) sigaction(SIGHUP, &old_hup, nullptr);
        };

        int pipe_out[2] = {-1, -1};
        int pipe_err[2] = {-1, -1};

        if (options.capture_output) {
            if (pipe(pipe_out) == -1) {
                restore_signals();
                throw std::runtime_error("pipe() failed for stdout!");
            }
            if (pipe(pipe_err) == -1) {
                close(pipe_out[0]);
                close(pipe_out[1]);
                restore_signals();
                throw std::runtime_error("pipe() failed for stderr!");
            }
        }

        // Sinais ficam bloqueados até o filho ter o estado final: nessa janela o
        // handler herdado rodaria na cópia do filho e o sinal se perderia.
        sigset_t block_set, old_mask;
        sigemptyset(&block_set);
        sigaddset(&block_set, SIGINT);
        sigaddset(&block_set, SIGQUIT);
        sigaddset(&block_set, SIGTERM);
        sigaddset(&block_set, SIGHUP);
        sigprocmask(SIG_BLOCK, &block_set, &old_mask);

        pid_t pid = fork();
        if (pid == -1) {
            sigprocmask(SIG_SETMASK, &old_mask, nullptr);
            if (options.capture_output) {
                close(pipe_out[0]); close(pipe_out[1]);
                close(pipe_err[0]); close(pipe_err[1]);
            }
            restore_signals();
            throw std::runtime_error("fork() failed!");
        }

        if (pid == 0) {
            // Cria um novo grupo de processos para que o filho seja líder do grupo.
            // Isso permite enviar sinais para toda a árvore de processos (ex: timeout).
            if (own_group) setpgid(0, 0);
            if (give_tty) {
                tcsetpgrp(tty_fd, getpid());
                // Não há controle de jobs: Ctrl+Z não tem como ser retomado.
                std::signal(SIGTSTP, SIG_IGN);
            }

            std::signal(SIGINT, SIG_DFL);
            std::signal(SIGQUIT, SIG_DFL);
            if (forward_term) std::signal(SIGTERM, SIG_DFL);
            if (forward_hup) std::signal(SIGHUP, SIG_DFL);
            if (give_tty) std::signal(SIGTTOU, SIG_DFL);
            sigprocmask(SIG_SETMASK, &old_mask, nullptr);

            if (options.redirect_stdin_null) {
                int devnull = open("/dev/null", O_RDONLY);
                if (devnull >= 0) {
                    dup2(devnull, STDIN_FILENO);
                    close(devnull);
                }
            }

            if (options.capture_output) {
                close(pipe_out[0]);
                close(pipe_err[0]);
                dup2(pipe_out[1], STDOUT_FILENO);
                close(pipe_out[1]);
                dup2(pipe_err[1], STDERR_FILENO);
                close(pipe_err[1]);
            }

            if (!options.cwd.empty()) {
                if (chdir(options.cwd.c_str()) != 0) {
                    _exit(127);
                }
            }

            for (const auto& [k, v] : options.env) {
                setenv(k.c_str(), v.c_str(), 1);
            }

            std::vector<char*> argv;
            for (const auto& arg : args) {
                argv.push_back(const_cast<char*>(arg.c_str()));
            }
            argv.push_back(nullptr);

            execvp(argv[0], argv.data());
            _exit(127);
        }

        // Repetido no pai: fecha a corrida com o filho antes de qualquer kill(-pid).
        if (own_group) {
            setpgid(pid, pid);
            g_active_child_pid = pid;
        }
        if (give_tty) tcsetpgrp(tty_fd, pid);
        sigprocmask(SIG_SETMASK, &old_mask, nullptr);

        ExecResult result;
        int status = 0;
        auto start_time = std::chrono::steady_clock::now();

        // No grupo próprio não há controle de jobs: um filho parado nunca terminaria
        // e o pai ficaria preso aqui. No grupo do shell, Ctrl+Z para os dois juntos.
        // Só retorna true quando o filho terminou de fato.
        const int wait_flags = own_group ? WUNTRACED : 0;
        auto reap = [&](int flags) -> bool {
            while (true) {
                int wp = waitpid(pid, &status, flags | wait_flags);
                if (wp == pid && WIFSTOPPED(status)) {
                    kill(-pid, SIGKILL);
                    continue;
                }
                if (wp == -1 && errno == EINTR) continue;
                return wp == pid || wp == -1;
            }
        };

        // O aethercli vai encerrar por sinal: o comando tem um prazo curto para
        // sair, senão um filho que ignora o sinal seguraria o pai indefinidamente.
        constexpr int EXIT_GRACE_MS = 2000;
        std::chrono::steady_clock::time_point exit_signal_time;
        bool exit_signal_seen = false;
        auto enforce_exit_grace = [&]() {
            if (g_pending_exit_signal == 0) return;
            auto now = std::chrono::steady_clock::now();
            if (!exit_signal_seen) {
                exit_signal_seen = true;
                exit_signal_time = now;
            } else if (std::chrono::duration_cast<std::chrono::milliseconds>(now - exit_signal_time).count() >= EXIT_GRACE_MS) {
                kill(-pid, SIGKILL);
            }
        };

        auto kill_process_group = [&]() {
            result.timed_out = true;
            // Envia SIGTERM para o grupo de processos (-pid)
            kill(-pid, SIGTERM);
            // Dá um intervalo de graça de até 500ms para terminar graciosamente
            bool reaped = false;
            for (int i = 0; i < 25 && !reaped; ++i) {
                reaped = reap(WNOHANG);
                if (!reaped) usleep(20000); // 20ms
            }
            // SIGKILL mesmo com o líder já encerrado: membros do grupo que
            // ignoraram SIGTERM não podem sobreviver ao timeout.
            kill(-pid, SIGKILL);
            if (!reaped) reap(0);
        };

        if (options.capture_output) {
            close(pipe_out[1]);
            close(pipe_err[1]);

            // Define modo não bloqueante nos descritores de leitura
            int flags_out = fcntl(pipe_out[0], F_GETFL, 0);
            if (flags_out >= 0) fcntl(pipe_out[0], F_SETFL, flags_out | O_NONBLOCK);
            int flags_err = fcntl(pipe_err[0], F_GETFL, 0);
            if (flags_err >= 0) fcntl(pipe_err[0], F_SETFL, flags_err | O_NONBLOCK);

            struct pollfd pfds[2];
            pfds[0].fd = pipe_out[0]; pfds[0].events = POLLIN; pfds[0].revents = 0;
            pfds[1].fd = pipe_err[0]; pfds[1].events = POLLIN; pfds[1].revents = 0;

            bool out_open = true;
            bool err_open = true;
            char buf[4096];

            auto read_fd = [&](int fd, std::string& out_str, bool& is_open) {
                while (true) {
                    ssize_t n = read(fd, buf, sizeof(buf));
                    if (n > 0) {
                        if (options.max_output_bytes > 0) {
                            if (out_str.size() < options.max_output_bytes) {
                                size_t allowed = options.max_output_bytes - out_str.size();
                                size_t to_append = std::min<size_t>(allowed, static_cast<size_t>(n));
                                out_str.append(buf, to_append);
                                if (to_append < static_cast<size_t>(n)) {
                                    result.output_truncated = true;
                                }
                            } else {
                                result.output_truncated = true;
                            }
                        } else {
                            out_str.append(buf, static_cast<size_t>(n));
                        }
                    } else if (n == 0) {
                        close(fd);
                        is_open = false;
                        break;
                    } else {
                        if (errno == EINTR) continue;
                        if (errno != EAGAIN && errno != EWOULDBLOCK) {
                            close(fd);
                            is_open = false;
                        }
                        break;
                    }
                }
            };

            // O laço acompanha o processo, não os pipes: um filho que fecha
            // stdout/stderr e continua rodando ainda está sujeito ao timeout.
            while (true) {
                int poll_timeout = (out_open || err_open) ? 200 : 20;
                if (options.timeout_s > 0) {
                    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - start_time
                    ).count();
                    long long remaining_ms = (options.timeout_s * 1000LL) - elapsed_ms;
                    if (remaining_ms <= 0) {
                        kill_process_group();
                        if (out_open) read_fd(pipe_out[0], result.stdout_output, out_open);
                        if (err_open) read_fd(pipe_err[0], result.stderr_output, err_open);
                        break;
                    }
                    poll_timeout = static_cast<int>(std::min<long long>(remaining_ms, poll_timeout));
                }

                pfds[0].fd = out_open ? pipe_out[0] : -1;
                pfds[1].fd = err_open ? pipe_err[0] : -1;
                int pr = poll(pfds, 2, poll_timeout);
                if (pr < 0 && errno == EINTR) continue;

                if (out_open) read_fd(pipe_out[0], result.stdout_output, out_open);
                if (err_open) read_fd(pipe_err[0], result.stderr_output, err_open);

                // Verifica se o processo filho terminou
                enforce_exit_grace();
                if (reap(WNOHANG)) {
                    // Drena os dados finais nos pipes
                    if (out_open) read_fd(pipe_out[0], result.stdout_output, out_open);
                    if (err_open) read_fd(pipe_err[0], result.stderr_output, err_open);
                    break;
                }
            }

            if (out_open) close(pipe_out[0]);
            if (err_open) close(pipe_err[0]);
        } else {
            // Modo sem captura direta (ex: shell interativo onde o filho desenha no terminal)
            if (options.timeout_s > 0) {
                while (true) {
                    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - start_time
                    ).count();
                    if (elapsed_ms >= options.timeout_s * 1000LL) {
                        kill_process_group();
                        break;
                    }
                    enforce_exit_grace();
                    if (reap(WNOHANG)) break;
                    usleep(20000); // 20ms
                }
            } else {
                reap(0);
            }
        }

        g_active_child_pid = 0;

        // SIGTTOU segue ignorado aqui: o shell ainda está em background.
        if (give_tty) tcsetpgrp(tty_fd, getpgrp());
        restore_signals();
        if (g_pending_exit_signal != 0) {
            const int pending = g_pending_exit_signal;
            g_pending_exit_signal = 0;
            raise(pending);
        }

        if (result.timed_out) {
            // Já marcado como timed_out
        } else if (WIFEXITED(status)) {
            result.exit_code = WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            result.signaled = true;
            result.term_signal = WTERMSIG(status);
        }

        return result;
    }

    static ExecResult executeShellCommand(const std::string& cmd, const ExecOptions& options = {}) {
        return execute({"/bin/sh", "-c", cmd}, options);
    }
};

} // namespace cli::executor
