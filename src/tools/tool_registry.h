#pragma once

#include "tools/tool_base.h"
#include <nlohmann/json.hpp>
#include <unordered_map>
#include <vector>
#include <string>
#include <memory>
#include <mutex>
#include <functional>

namespace claude {

// ============================================================
// Tool Registry - Manages all available tools
// ============================================================

class ToolRegistry {
public:
    ToolRegistry() = default;

    // Register a tool (takes ownership)
    void register_tool(std::unique_ptr<ToolBase> tool);

    // Register a tool factory (lazy creation)
    using ToolFactory = std::function<std::unique_ptr<ToolBase>()>;
    void register_factory(const std::string& name, ToolFactory factory);

    // Find tool by name
    ToolBase* find_tool(const std::string& name) const;

    // List all registered tool names
    std::vector<std::string> tool_names() const;

    // List tools by category
    std::vector<ToolBase*> tools_by_category(const std::string& category) const;

    // Generate JSON array of tool schemas for API request
    nlohmann::json tools_json_array() const;

    // Legacy: string version
    std::string tools_json_schema_array() const;

    // Generate system prompt section describing all tools
    std::string tools_system_prompt() const;

    // Number of registered tools
    size_t size() const { return tools_.size(); }

    // Check if tool exists
    bool has_tool(const std::string& name) const {
        return tools_.find(name) != tools_.end();
    }

    // Iterator support
    auto begin() const { return tools_.begin(); }
    auto end() const { return tools_.end(); }

private:
    std::unordered_map<std::string, std::unique_ptr<ToolBase>> tools_;
    mutable std::mutex mutex_;
};

}  // namespace claude
