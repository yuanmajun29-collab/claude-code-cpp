#pragma once

#include <string>
#include <vector>
#include <optional>
#include <functional>

namespace claude {
namespace util {

struct ProcessResult {
    int exit_code = -1;
    std::string stdout_output;
    std::string stderr_output;
    bool timed_out = false;
    bool success() const { return exit_code == 0 && !timed_out; }

    // Time stats
    double wall_time_ms = 0.0;
};

struct ProcessOptions {
    int timeout_seconds = 120;
    std::string working_directory;
    std::vector<std::string> extra_env;
    bool merge_stderr = false;  // If true, merge stderr into stdout
    bool capture_output = true;
    size_t max_output_bytes = 50000;  // Per-stream truncation limit
    std::string stdin_input;  // Optional stdin content
};

// Execute a shell command string
ProcessResult exec_command(const std::string& command,
                           const ProcessOptions& options = {});

// Execute a program with args array
ProcessResult exec_program(const std::string& program,
                            const std::vector<std::string>& args,
                            const ProcessOptions& options = {});

// Execute with full control (fork/exec)
ProcessResult exec_fork(const std::string& program,
                         const std::vector<std::string>& args,
                         const ProcessOptions& options = {});

// Check if a program exists in PATH
bool command_exists(const std::string& program);

// Get/set current working directory
std::string current_working_directory();
bool set_cwd(const std::string& path);

// Kill a process tree (send SIGTERM, wait, then SIGKILL)
int kill_process_tree(pid_t pid, int grace_period_ms = 5000);

}  // namespace util
}  // namespace claude
