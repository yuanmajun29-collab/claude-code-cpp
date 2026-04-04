#include "tools/mcp_tool.h"
#include "tools/tool_registry.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <sstream>

namespace claude {

// ============================================================
// McpTool — proxies a single MCP tool
// ============================================================

McpTool::McpTool(const std::string& server_name,
                 const mcp::McpToolDef& tool_def,
                 mcp::McpClient* client)
    : server_name_(server_name), tool_def_(tool_def), client_(client) {}

std::string McpTool::name() const {
    // Prefix with server name to avoid collisions: "server__tool"
    return server_name_ + "__" + tool_def_.name;
}

std::string McpTool::description() const {
    return tool_def_.description;
}

ToolInputSchema McpTool::input_schema() const {
    ToolInputSchema schema;
    schema.type = "object";

    if (tool_def_.input_schema.is_object()) {
        auto props = tool_def_.input_schema.value("properties", nlohmann::json::object());
        for (auto& [key, val] : props.items()) {
            if (val.is_object()) {
                std::string type = val.value("type", "string");
                std::string desc = val.value("description", "");
                schema.properties[key] = {type, desc};
            } else {
                schema.properties[key] = {"string", ""};
            }
        }

        auto required = tool_def_.input_schema.value("required", nlohmann::json::array());
        for (const auto& r : required) {
            if (r.is_string()) schema.required.push_back(r.get<std::string>());
        }
    }

    return schema;
}

ToolOutput McpTool::execute(const std::string& input_json, ToolContext& /*ctx*/) {
    if (!client_) {
        return ToolOutput::err("MCP server '" + server_name_ + "' is not connected");
    }

    try {
        auto input = nlohmann::json::parse(input_json);
        auto result = client_->call_tool(tool_def_.name, input);

        if (result.is_error) {
            return ToolOutput::err(result.error_message);
        }
        if (!result.success) {
            return ToolOutput::err("MCP tool call failed: " + result.error_message);
        }

        // Extract text content from MCP result
        std::string output;
        if (result.content.is_array()) {
            for (const auto& block : result.content) {
                if (block.is_object()) {
                    if (block.value("type", "") == "text") {
                        output += block.value("text", "");
                    } else if (block.value("type", "") == "image") {
                        output += "[Image: " + block.value("mimeType", "unknown") + "]";
                    } else if (block.value("type", "") == "resource") {
                        output += block.value("text", block.dump());
                    }
                } else if (block.is_string()) {
                    output += block.get<std::string>();
                }
            }
        } else if (result.content.is_string()) {
            output = result.content.get<std::string>();
        } else if (!result.content.is_null()) {
            output = result.content.dump();
        }

        return ToolOutput::ok(output);
    } catch (const std::exception& e) {
        return ToolOutput::err(std::string("MCP tool error: ") + e.what());
    }
}

void McpTool::update_definition(const mcp::McpToolDef& new_def) {
    tool_def_ = new_def;
}

// ============================================================
// McpToolManager — syncs MCP tools into ToolRegistry
// ============================================================

McpToolManager::McpToolManager(ToolRegistry& registry, mcp::McpManager& mcp_manager)
    : registry_(registry), mcp_manager_(mcp_manager) {}

void McpToolManager::sync_tools() {
    std::lock_guard<std::mutex> lock(mutex_);
    auto names = mcp_manager_.server_names();
    for (const auto& name : names) {
        sync_server_tools_impl(name);
    }
}

void McpToolManager::sync_server_tools(const std::string& server_name) {
    std::lock_guard<std::mutex> lock(mutex_);
    sync_server_tools_impl(server_name);
}

void McpToolManager::remove_server_tools(const std::string& server_name) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = server_to_tools_.find(server_name);
    if (it == server_to_tools_.end()) return;

    for (const auto& tool_name : it->second) {
        tool_to_server_.erase(tool_name);
        // Remove from registry - registry doesn't have unregister, so we note it
        spdlog::debug("MCP tool removed: {} (server: {})", tool_name, server_name);
    }
    server_to_tools_.erase(it);
}

size_t McpToolManager::tool_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return tool_to_server_.size();
}

bool McpToolManager::is_mcp_tool(const std::string& tool_name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return tool_to_server_.find(tool_name) != tool_to_server_.end();
}

std::optional<std::string> McpToolManager::tool_server(const std::string& tool_name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = tool_to_server_.find(tool_name);
    if (it != tool_to_server_.end()) return it->second;
    return std::nullopt;
}

void McpToolManager::sync_server_tools_impl(const std::string& server_name) {
    // Remove existing tools from this server
    remove_server_tools_impl(server_name);

    auto tools = mcp_manager_.list_tools(server_name);
    if (tools.empty()) return;

    auto* client = mcp_manager_.get_client(server_name);
    if (!client) return;

    std::vector<std::string> registered_names;

    for (const auto& tool_def : tools) {
        auto tool = std::make_unique<McpTool>(server_name, tool_def, client);
        std::string tool_name = tool->name();

        // Skip if a tool with this name already exists (non-MCP)
        if (registry_.has_tool(tool_name)) {
            spdlog::warn("MCP tool name conflict: {} (from server {})", tool_name, server_name);
            continue;
        }

        registry_.register_tool(std::move(tool));
        tool_to_server_[tool_name] = server_name;
        registered_names.push_back(tool_name);
    }

    server_to_tools_[server_name] = std::move(registered_names);
    spdlog::info("Synced {} MCP tools from server '{}'",
                 server_to_tools_[server_name].size(), server_name);
}

void McpToolManager::remove_server_tools_impl(const std::string& server_name) {
    auto it = server_to_tools_.find(server_name);
    if (it == server_to_tools_.end()) return;

    for (const auto& tool_name : it->second) {
        tool_to_server_.erase(tool_name);
    }
    server_to_tools_.erase(it);
}

}  // namespace claude
