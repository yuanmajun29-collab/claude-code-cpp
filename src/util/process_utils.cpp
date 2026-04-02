#include "util/process_utils.h"
#include "util/string_utils.h"
#include <spdlog/spdlog.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <thread>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <csignal>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/poll.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <cstring>

namespace claude {
namespace util {

namespace fs = std::filesystem;

// ============================================================
// Process tree killing
// ============================================================

int kill_process_tree(pid_t pid, int grace_period_ms) {
    // First, try SIGTERM
    killpg(pid, SIGTERM);
    auto start = std::chrono::steady_clock::now();

    while (true) {
        int status;
        pid_t ret = waitpid(pid, &status, WNOHANG);
        if (ret == pid) return WEXITSTATUS(status);
        if (ret == -1) return -1;

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        if (elapsed >= grace_period_ms) {
            // Force kill
            killpg(pid, SIGKILL);
            waitpid(pid, &status, 0);
            return WEXITSTATUS(status);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

// ============================================================
// Fork-based execution (most reliable)
// ============================================================

ProcessResult exec_fork(const std::string& program,
                         const std::vector<std::string>& args,
                         const ProcessOptions& options) {
    ProcessResult result;
    auto start = std::chrono::steady_clock::now();

    // Create pipes for stdout and stderr
    int stdout_pipe[2] = {-1, -1};
    int stderr_pipe[2] = {-1, -1};
    int stdin_pipe[2] = {-1, -1};

    if (options.capture_output) {
        if (pipe(stdout_pipe) != 0 || (!options.merge_stderr && pipe(stderr_pipe) != 0)) {
            return ProcessResult{-1, "", "Failed to create pipes: " + std::string(strerror(errno)), false};
        }
    }

    if (!options.stdin_input.empty()) {
        if (pipe(stdin_pipe) != 0) {
            return ProcessResult{-1, "", "Failed to create stdin pipe", false};
        }
    }

    // Build argv
    std::vector<char*> argv;
    argv.push_back(const_cast<char*>(program.c_str()));
    for (const auto& arg : args) {
        argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);

    pid_t pid = fork();
    if (pid < 0) {
        return ProcessResult{-1, "", "Fork failed: " + std::string(strerror(errno)), false};
    }

    if (pid == 0) {
        // Child process
        // Create new process group for killing the tree
        setpgid(0, 0);

        // Set working directory
        if (!options.working_directory.empty()) {
            if (chdir(options.working_directory.c_str()) != 0) {
                _exit(127);
            }
        }

        // Setup stdout
        if (options.capture_output) {
            dup2(stdout_pipe[1], STDOUT_FILENO);
            close(stdout_pipe[0]);
            close(stdout_pipe[1]);
            if (options.merge_stderr) {
                dup2(STDOUT_FILENO, STDERR_FILENO);
            } else {
                dup2(stderr_pipe[1], STDERR_FILENO);
                close(stderr_pipe[0]);
                close(stderr_pipe[1]);
            }
        }

        // Setup stdin
        if (!options.stdin_input.empty()) {
            dup2(stdin_pipe[0], STDIN_FILENO);
            close(stdin_pipe[0]);
            close(stdin_pipe[1]);
        }

        // Set PATH and HOME if not in environment
        setenv("PATH", "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin", 1);
        if (getenv("HOME") == nullptr) setenv("HOME", "/root", 1);
        if (getenv("TERM") == nullptr) setenv("TERM", "xterm-256color", 1);

        // Extra environment
        for (const auto& env : options.extra_env) {
            putenv(const_cast<char*>(env.c_str()));
        }

        execvp(program.c_str(), argv.data());
        _exit(127);  // exec failed
    }

    // Parent process
    close(stdout_pipe[1]);
    if (!options.merge_stderr) close(stderr_pipe[1]);
    if (!options.stdin_input.empty()) {
        close(stdin_pipe[0]);
        // Write stdin
        auto n = write(stdin_pipe[1], options.stdin_input.data(), options.stdin_input.size());
        (void)n;
        close(stdin_pipe[1]);
    }

    // Read stdout and stderr with timeout
    auto read_pipe = [](int fd, std::string& buffer, size_t max_bytes) {
        std::array<char, 4096> buf;
        while (true) {
            ssize_t n = read(fd, buf.data(), buf.size());
            if (n <= 0) break;
            if (buffer.size() + n > max_bytes) {
                size_t remaining = max_bytes - buffer.size();
                if (remaining > 0) buffer.append(buf.data(), remaining);
                break;
            }
            buffer.append(buf.data(), n);
        }
    };

    // Use poll for timeout-aware reading
    struct pollfd fds[2];
    int nfds = 0;

    if (options.capture_output) {
        fds[nfds].fd = stdout_pipe[0];
        fds[nfds].events = POLLIN;
        fds[nfds].revents = 0;
        nfds++;
    }
    if (!options.merge_stderr && options.capture_output) {
        fds[nfds].fd = stderr_pipe[0];
        fds[nfds].events = POLLIN;
        fds[nfds].revents = 0;
        nfds++;
    }

    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::seconds(options.timeout_seconds > 0 ? options.timeout_seconds : 120);

    while (nfds > 0) {
        auto now = std::chrono::steady_clock::now();
        int timeout_ms = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count());
        if (timeout_ms <= 0) {
            result.timed_out = true;
            kill_process_tree(pid);
            break;
        }

        int ret = poll(fds, nfds, timeout_ms);
        if (ret < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (ret == 0) {
            result.timed_out = true;
            kill_process_tree(pid);
            break;
        }

        for (int i = 0; i < nfds; ) {
            if (fds[i].revents & (POLLIN | POLLHUP)) {
                if (fds[i].fd == stdout_pipe[0]) {
                    read_pipe(stdout_pipe[0], result.stdout_output, options.max_output_bytes);
                    close(stdout_pipe[0]);
                    fds[i] = fds[nfds - 1];
                    nfds--;
                    continue;
                } else if (fds[i].fd == stderr_pipe[0]) {
                    read_pipe(stderr_pipe[0], result.stderr_output, options.max_output_bytes);
                    close(stderr_pipe[0]);
                    fds[i] = fds[nfds - 1];
                    nfds--;
                    continue;
                }
            }
            i++;
        }
    }

    // Close any remaining pipe fds
    if (stdout_pipe[0] >= 0) close(stdout_pipe[0]);
    if (stderr_pipe[0] >= 0) close(stderr_pipe[0]);

    // Wait for child
    if (!result.timed_out) {
        int status;
        pid_t ret;
        // Check if already exited
        do {
            ret = waitpid(pid, &status, 0);
        } while (ret == -1 && errno == EINTR);
        result.exit_code = (ret == pid && WIFEXITED(status)) ? WEXITSTATUS(status) : -1;
    } else {
        int status;
        waitpid(pid, &status, 0);
        result.exit_code = -1;
    }

    auto end = std::chrono::steady_clock::now();
    result.wall_time_ms = std::chrono::duration<double, std::milli>(end - start).count();

    return result;
}

// ============================================================
// High-level wrappers
// ============================================================

ProcessResult exec_program(const std::string& program,
                            const std::vector<std::string>& args,
                            const ProcessOptions& options) {
    return exec_fork(program, args, options);
}

ProcessResult exec_command(const std::string& command,
                           const ProcessOptions& options) {
    // Write command to temp script file
    char tmp_buf[] = "/tmp/claude_exec_XXXXXX";
    int fd = mkstemp(tmp_buf);
    if (fd < 0) {
        return ProcessResult{-1, "", "Failed to create temp file", false};
    }
    close(fd);

    {
        std::ofstream f(tmp_buf);
        f << "#!/bin/bash\n"
          << "set -o pipefail\n"
          << command << "\n";
    }
    fs::permissions(tmp_buf, fs::perms::owner_exec | fs::perms::owner_read, fs::perm_options::replace);

    auto result = exec_fork("/bin/bash", {tmp_buf}, options);

    // Cleanup
    fs::remove(tmp_buf);
    return result;
}

bool command_exists(const std::string& program) {
    ProcessOptions opts;
    opts.working_directory = ".";
    opts.capture_output = false;
    opts.merge_stderr = true;
    auto result = exec_program("which", {program}, opts);
    return result.exit_code == 0;
}

std::string current_working_directory() {
    std::error_code ec;
    auto p = fs::current_path(ec);
    return ec ? "." : p.string();
}

bool set_cwd(const std::string& path) {
    std::error_code ec;
    fs::current_path(path, ec);
    return !ec;
}

}  // namespace util
}  // namespace claude
