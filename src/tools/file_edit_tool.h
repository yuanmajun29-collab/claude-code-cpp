#pragma once

#include "tools/tool_base.h"
#include <nlohmann/json.hpp>

namespace claude {

// File editing tool (most complex tool)
// Supports exact text replacement with old_string -> new_string
class FileEditTool : public ToolBase {
public:
    FileEditTool() = default;
    ~FileEditTool() override = default;

    std::string name() const override { return "Edit"; }
    std::string description() const override {
        return "Edit a file using exact text replacement. Use old_string/new_string "
               "for single replacement, or edits array for multiple separate replacements.";
    }

    ToolInputSchema input_schema() const override;
    std::string system_prompt() const override;
    ToolOutput execute(const std::string& input_json, ToolContext& ctx) override;

    std::string category() const override { return "file"; }

private:
    // Single replacement
    ToolOutput do_single_edit(const std::string& file_path,
                               const std::string& old_string,
                               const std::string& new_string,
                               const std::string& cwd);

    // Multiple replacements
    ToolOutput do_multi_edit(const std::string& file_path,
                              const nlohmann::json& edits_array,
                              const std::string& cwd);

    // Apply a single replacement to file content
    std::optional<std::string> apply_replacement(const std::string& content,
                                                   const std::string& old_string,
                                                   const std::string& new_string,
                                                   int& match_count);

    // Generate a unified diff for display
    std::string generate_diff(const std::string& old_content,
                               const std::string& new_content,
                               const std::string& file_path);
};

}  // namespace claude
