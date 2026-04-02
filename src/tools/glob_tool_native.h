#pragma once

#include "tools/tool_base.h"
#include <string>
#include <vector>
#include <regex>

namespace claude {

// Native glob pattern matching tool
class GlobTool : public ToolBase {
public:
    GlobTool() = default;
    ~GlobTool() override = default;

    std::string name() const override { return "Glob"; }
    std::string description() const override {
        return "Find files matching glob patterns using native C++ implementation. "
               "Supports *, **, ?, [] patterns.";
    }

    ToolInputSchema input_schema() const override;
    std::string system_prompt() const override;
    ToolOutput execute(const std::string& input_json, ToolContext& ctx) override;

    bool requires_confirmation() const override { return false; }
    std::string category() const override { return "filesystem"; }

private:
    // Default exclude patterns
    static const std::vector<std::string>& exclude_patterns();

    // Convert glob pattern to regex
    std::string glob_to_regex(const std::string& glob) const;

    // Check if path should be excluded
    bool is_excluded(const std::string& path, const std::string& base_dir) const;

    // Match single pattern against a path
    bool match_pattern(const std::string& pattern, const std::string& path) const;

    // Recursive glob matching
    std::vector<std::string> glob_recursive(
        const std::string& base_dir,
        const std::string& pattern,
        bool recursive) const;

    // Split pattern into directory and file parts
    std::pair<std::string, std::string> split_pattern(const std::string& pattern) const;

    // Check if pattern is recursive (contains **)
    bool is_recursive_pattern(const std::string& pattern) const;
};

}  // namespace claude
