#pragma once

#include "tools/tool_base.h"

namespace claude {

// File writing tool
class FileWriteTool : public ToolBase {
public:
    FileWriteTool() = default;
    ~FileWriteTool() override = default;

    std::string name() const override { return "Write"; }
    std::string description() const override {
        return "Write content to a file, creating it if it doesn't exist. "
               "Overwrites the entire file content.";
    }

    ToolInputSchema input_schema() const override;
    std::string system_prompt() const override;
    ToolOutput execute(const std::string& input_json, ToolContext& ctx) override;

    std::string category() const override { return "file"; }
};

}  // namespace claude
