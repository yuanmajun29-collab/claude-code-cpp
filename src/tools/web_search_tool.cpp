#include "tools/web_search_tool.h"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace claude {

ToolInputSchema WebSearchTool::input_schema() const {
    ToolInputSchema schema;
    schema.type = "object";
    schema.properties["query"] = "string — Search query string";
    schema.required = {"query"};
    return schema;
}

std::string WebSearchTool::system_prompt() const {
    return "Search the web using a search API. Returns titles, URLs, and snippets.";
}

ToolOutput WebSearchTool::execute(const std::string& input_json, ToolContext& /*ctx*/) {
    try {
        auto j = json::parse(input_json);
        std::string query = j.value("query", "");

        if (query.empty()) {
            return ToolOutput::err("No query specified");
        }

        // Stub implementation for P0
        return ToolOutput::err("WebSearch tool not fully implemented in P0. Query: " + query);
    } catch (const json::parse_error& e) {
        return ToolOutput::err("Invalid JSON input: " + std::string(e.what()));
    }
}

}  // namespace claude
