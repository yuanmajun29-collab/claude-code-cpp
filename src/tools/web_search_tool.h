#pragma once

#include "tools/tool_base.h"

namespace claude {

// Web search tool
class WebSearchTool : public ToolBase {
public:
    WebSearchTool() = default;
    ~WebSearchTool() override = default;

    std::string name() const override { return "WebSearch"; }
    std::string description() const override {
        return "Search the web for information. Returns titles, URLs, and snippets.";
    }

    ToolInputSchema input_schema() const override;
    std::string system_prompt() const override;
    ToolOutput execute(const std::string& input_json, ToolContext& ctx) override;

    std::string category() const override { return "web"; }
};

}  // namespace claude
