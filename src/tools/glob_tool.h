#pragma once

#include "tools/tool_base.h"

namespace claude {

// Glob/file pattern matching tool
class GlobTool : public ToolBase {
public:
    GlobTool() = default;
    ~GlobTool() override = default;

    std::string name() const override { return "Glob"; }
    std::string description() const override {
        return "Find files matching a glob pattern. Supports *, **, ?, [] patterns. "
               "Excludes .git, node_modules, and other common ignores.";
    }

    ToolInputSchema input_schema() const override;
    std::string system_prompt() const override;
    ToolOutput execute(const std::string& input_json, ToolContext& ctx) override;

    std::string category() const override { return "file"; }

private:
    // Default exclude patterns
    static const std::vector<std::string>& exclude_patterns();
};

}  // namespace claude
