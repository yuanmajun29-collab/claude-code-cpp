#pragma once

#include "tools/tool_base.h"
#include "protocol/mcp/mcp_client.h"
#include <memory>
#include <string>
#include <vector>
#include <mutex>
#include <optional>

namespace claude {

// Forward declarations
class ToolRegistry;

// MCP Tool — proxies a tool from an MCP server into the local ToolRegistry
class McpTool : public ToolBase {
public:
    McpTool(const std::string& server_name,
            const mcp::McpToolDef& tool_def,
            mcp::McpClient* client);

    std::string name() const override;
    std::string description() const override;
    ToolInputSchema input_schema() const override;
    std::string system_prompt() const override { return description(); }
    ToolOutput execute(const std::string& input_json, ToolContext& ctx) override;
    std::string category() const override { return "mcp"; }

    std::string server_name() const { return server_name_; }
    std::string mcp_tool_name() const { return tool_def_.name; }
    void update_definition(const mcp::McpToolDef& new_def);

private:
    std::string server_name_;
    mcp::McpToolDef tool_def_;
    mcp::McpClient* client_;
};

// MCP Tool Manager
class McpToolManager {
public:
    McpToolManager(ToolRegistry& registry, mcp::McpManager& mcp_manager);

    void sync_tools();
    void sync_server_tools(const std::string& server_name);
    void remove_server_tools(const std::string& server_name);
    size_t tool_count() const;
    bool is_mcp_tool(const std::string& tool_name) const;
    std::optional<std::string> tool_server(const std::string& tool_name) const;

private:
    void sync_server_tools_impl(const std::string& server_name);
    void remove_server_tools_impl(const std::string& server_name);

    ToolRegistry& registry_;
    mcp::McpManager& mcp_manager_;
    std::map<std::string, std::string> tool_to_server_;
    std::map<std::string, std::vector<std::string>> server_to_tools_;
    mutable std::mutex mutex_;
};

}  // namespace claude
