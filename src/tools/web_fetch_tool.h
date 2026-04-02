#pragma once

#include "tools/tool_base.h"

namespace claude {

// Web fetch tool - fetches URLs using libcurl
class WebFetchTool : public ToolBase {
public:
    WebFetchTool() = default;
    ~WebFetchTool() override = default;

    std::string name() const override { return "WebFetch"; }
    std::string description() const override {
        return "Fetch and extract readable content from a URL (HTML → markdown/text). "
               "Use for lightweight page access without browser automation.";
    }

    ToolInputSchema input_schema() const override;
    std::string system_prompt() const override;
    ToolOutput execute(const std::string& input_json, ToolContext& ctx) override;

    std::string category() const override { return "web"; }

private:
    // Fetch URL content
    std::optional<std::string> fetch_url(const std::string& url, int timeout_seconds = 30);

    // Strip HTML tags (simple approach)
    std::string html_to_text(const std::string& html) const;

    // Extract title from HTML
    std::string extract_title(const std::string& html) const;
};

}  // namespace claude
