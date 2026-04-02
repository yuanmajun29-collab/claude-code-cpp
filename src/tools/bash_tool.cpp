#include "tools/bash_tool.h"
#include "util/process_utils.h"
#include "util/file_utils.h"
#include "util/string_utils.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <filesystem>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace claude {

ToolInputSchema BashTool::input_schema() const {
    ToolInputSchema schema;
    schema.type = "object";
    schema.properties["command"] = "string — The shell command to execute";
    schema.properties["timeout"] = "number — Timeout in seconds (default 120)";
    schema.properties["description"] = "string — Optional description of what the command does";
    schema.required = {"command"};
    return schema;
}

std::string BashTool::system_prompt() const {
    return R"(The Bash tool executes shell commands in the working directory.

**Usage:** Pass the command to execute as the "command" parameter.

**Guidelines:**
- Prefer single commands over complex pipelines when possible
- Use && for dependent commands
- Commands have a 120-second timeout by default
- Output is limited to 50,000 chars per stream to prevent excessive token usage

**Environment:**
- PATH, HOME, TERM are set automatically
- Working directory persists between commands (cd is sticky)
- Each command runs in a fresh bash subprocess

**Safety:**
- Some dangerous commands require user confirmation
- Never try to bypass safety restrictions
- The command timeout can be extended via the "timeout" parameter (max 600s))";
}

bool BashTool::is_dangerous(const std::string& command) const {
    std::string lower = util::to_lower(command);
    // Strip comments
    auto pos = lower.find('#');
    if (pos != std::string::npos) lower = lower.substr(0, pos);

    // Dangerous patterns
    static const std::vector<std::string> dangerous = {
        "rm -rf /", "rm -rf /*",
        "mkfs.", "dd if=/dev/zero",
        ":(){ :|:& };:",           // fork bomb
        "> /dev/sda", "> /dev/sdb",
        "chmod -r 777 /",
        "mv / /dev/null",
        "> /etc/passwd",
        "shutdown ", "reboot ", "halt ", "poweroff ",
        "iptables -F",
        ":(){}",                    // fork bomb variant
        "\\nc / ",                 // netcat
    };

    for (const auto& pattern : dangerous) {
        if (lower.find(pattern) != std::string::npos) return true;
    }
    return false;
}

std::string BashTool::get_danger_warning(const std::string& /*command*/) const {
    return "This command appears to be potentially dangerous. Execution denied for safety.";
}

std::string BashTool::truncate_output(const std::string& output, size_t max_bytes) const {
    if (output.size() <= max_bytes) return output;
    return output.substr(0, max_bytes) +
           "\n\n[Output truncated: " + std::to_string(output.size()) +
           " chars total, showing first " + std::to_string(max_bytes) + "]";
}

ToolOutput BashTool::execute(const std::string& input_json, ToolContext& ctx) {
    try {
        auto j = json::parse(input_json);
        std::string command = j.value("command", "");
        int timeout = j.value("timeout", 120);

        if (command.empty()) {
            return ToolOutput::err("No command specified");
        }

        // Clamp timeout
        timeout = std::min(timeout, 600);
        if (timeout < 1) timeout = 120;

        spdlog::debug("Bash executing ({}s timeout): {}", timeout, command);

        // Check for dangerous commands
        if (is_dangerous(command)) {
            auto perm = ctx.check_permission(name(), input_json);
            if (perm == PermissionResult::Denied) {
                return ToolOutput::err(get_danger_warning(command));
            }
        }

        // Handle cd commands — track working directory
        std::string trimmed_cmd = util::trim(command);
        if (util::starts_with(trimmed_cmd, "cd ")) {
            std::string target = trimmed_cmd.substr(3);
            util::trim(target);
            // Expand ~
            fs::path new_dir = util::expand_home(target);
            if (new_dir.is_relative()) {
                new_dir = fs::path(last_working_directory_.empty() ? ctx.working_directory : last_working_directory_) / new_dir;
            }
            std::error_code ec;
            new_dir = fs::canonical(new_dir, ec);
            if (ec || !fs::is_directory(new_dir)) {
                return ToolOutput::err("No such directory: " + target);
            }
            last_working_directory_ = new_dir.string();
            return ToolOutput::ok("Changed directory to " + last_working_directory_);
        }

        // Setup process options
        util::ProcessOptions opts;
        opts.working_directory = last_working_directory_.empty() ?
                                   ctx.working_directory : last_working_directory_;
        opts.timeout_seconds = timeout;
        opts.max_output_bytes = 50000;
        opts.merge_stderr = false;

        auto result = util::exec_command(command, opts);

        // Format output
        std::ostringstream output;

        // stdout
        if (!result.stdout_output.empty()) {
            output << truncate_output(result.stdout_output, 50000);
        }

        // stderr (show if non-empty)
        if (!result.stderr_output.empty()) {
            if (!output.str().empty()) output << "\n";
            output << "[stderr]\n" << truncate_output(result.stderr_output, 10000);
        }

        if (result.timed_out) {
            output << "\n\nCommand timed out after " << std::to_string(timeout) << " seconds";
            return ToolOutput{true, output.str(), "Timeout", true};
        }

        if (result.exit_code != 0) {
            output << "\n\nCommand exited with code " << std::to_string(result.exit_code);
            output << " (" << util::format_duration(result.wall_time_ms) << ")";
            return ToolOutput{true, output.str(), "", true};
        }

        // Success — add timing
        output << "\n(" << util::format_duration(result.wall_time_ms) << ")";

        return ToolOutput::ok(output.str());

    } catch (const json::parse_error& e) {
        return ToolOutput::err("Invalid JSON input: " + std::string(e.what()));
    } catch (const std::exception& e) {
        return ToolOutput::err(std::string("Error: ") + e.what());
    }
}

}  // namespace claude
