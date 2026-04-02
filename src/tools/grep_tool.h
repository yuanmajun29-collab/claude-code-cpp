#pragma once

#include "tools/tool_base.h"

namespace claude {

// Grep/content search tool (calls ripgrep)
class GrepTool : public ToolBase {
public:
    GrepTool() = default;
    ~GrepTool() override = default;

    std::string name() const override { return "Grep"; }
    std::string description() const override {
        return "Search file contents using regex patterns. Wraps ripgrep (rg) "
               "for fast search with context lines, file type filtering, and more.";
    }

    ToolInputSchema input_schema() const override;
    std::string system_prompt() const override;
    ToolOutput execute(const std::string& input_json, ToolContext& ctx) override;

    std::string category() const override { return "file"; }
};

}  // namespace claude
