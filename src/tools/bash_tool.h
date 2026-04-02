#pragma once

#include "tools/tool_base.h"
#include <string>
#include <vector>
#include <optional>

namespace claude {

// Bash/shell command execution tool
class BashTool : public ToolBase {
public:
    BashTool() = default;
    ~BashTool() override = default;

    std::string name() const override { return "Bash"; }
    std::string description() const override {
        return "Execute shell commands in the working directory. "
               "Use this for running build commands, git operations, "
               "and other terminal tasks.";
    }

    ToolInputSchema input_schema() const override;
    std::string system_prompt() const override;
    ToolOutput execute(const std::string& input_json, ToolContext& ctx) override;

    bool requires_confirmation() const override { return true; }
    std::string category() const override { return "execution"; }

private:
    // Dangerous command patterns
    bool is_dangerous(const std::string& command) const;
    std::string get_danger_warning(const std::string& command) const;

    // Truncate output with notice
    std::string truncate_output(const std::string& output, size_t max_bytes) const;

    // Working directory tracking
    std::string last_working_directory_;
};

}  // namespace claude
