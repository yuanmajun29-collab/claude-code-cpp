#pragma once

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <memory>
#include <optional>
#include <mutex>
#include <nlohmann/json.hpp>
#include <curl/curl.h>

namespace claude {
namespace mcp {

using json = nlohmann::json;

// MCP JSON-RPC message types
struct JsonRpcMessage {
    std::string jsonrpc = "2.0";
    std::string id;
    std::string method;
    json params;
    json result;
    json error;

    std::string to_string() const;
    static JsonRpcMessage parse(const std::string& raw);
};

// MCP Tool definition
struct McpToolDef {
    std::string name;
    std::string description;
    json input_schema;  // JSON Schema
};

// MCP Resource
struct McpResource {
    std::string uri;
    std::string name;
    std::string description;
    std::string mime_type;
};

// MCP Server configuration
struct McpServerConfig {
    std::string name;
    std::string command;        // For stdio transport
    std::vector<std::string> args;
    std::map<std::string, std::string> env;
    std::string url;            // For SSE transport
    std::string transport = "stdio";  // "stdio" or "sse"
    int timeout_seconds = 30;
    bool enabled = true;
};

// MCP Tool call result
struct McpToolResult {
    bool success = false;
    json content;     // Array of content blocks
    bool is_error = false;
    std::string error_message;
};

// MCP Client - communicates with a single MCP server
class McpClient {
public:
    explicit McpClient(const McpServerConfig& config);
    ~McpClient();

    // Lifecycle
    bool connect();
    void disconnect();
    bool is_connected() const { return connected_; }

    // Capabilities
    std::string server_name() const { return server_name_; }
    std::string server_version() const { return server_version_; }

    // Tools
    std::vector<McpToolDef> list_tools();
    McpToolResult call_tool(const std::string& name, const json& arguments);

    // Resources
    std::vector<McpResource> list_resources();
    std::optional<std::string> read_resource(const std::string& uri);

    // Prompts
    json list_prompts();
    json get_prompt(const std::string& name, const json& arguments = {});

    // Server info
    const McpServerConfig& config() const { return config_; }

private:
    // JSON-RPC communication
    JsonRpcMessage send_request(const std::string& method, const json& params = json{});
    void send_notification(const std::string& method, const json& params = json{});

    // Transport
    bool connect_stdio();
    bool connect_sse();

    // SSE helpers
    std::string send_sse_request(const std::string& message);
    bool send_sse_notification(const std::string& message);

    // Read from transport
    std::string read_response(int timeout_ms = 30000);
    void write_message(const std::string& message);

    // curl write callback
    static size_t curl_write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
        auto* response = static_cast<std::string*>(userdata);
        size_t total = size * nmemb;
        response->append(ptr, total);
        return total;
    }

    // Transport type
    enum class TransportType { Stdio, SSE };
    TransportType transport_type_ = TransportType::Stdio;

    // Process incoming messages
    void process_message(const JsonRpcMessage& msg);

    McpServerConfig config_;
    bool connected_ = false;
    std::string server_name_;
    std::string server_version_;

    // Process handle for stdio
    pid_t child_pid_ = -1;
    int write_fd_ = -1;
    int read_fd_ = -1;

    // Request tracking
    std::mutex mutex_;
    int next_id_ = 1;
    std::map<std::string, json> pending_requests_;
    std::map<std::string, json> pending_results_;

    // SSE state
    int curl_handle_ = 0;  // opaque handle indicator
    std::string sse_session_id_;
    std::string post_endpoint_;
    std::unique_ptr<class McpSseTransport> sse_transport_;  // Forward: defined in mcp_transport.h
};

// MCP Manager - manages all MCP server connections
class McpManager {
public:
    McpManager() = default;
    ~McpManager();

    // Server management
    void add_server(const McpServerConfig& config);
    bool remove_server(const std::string& name);
    std::vector<std::string> server_names() const;

    // Connect/disconnect all
    bool connect_all();
    void disconnect_all();
    bool connect_server(const std::string& name);
    void disconnect_server(const std::string& name);

    // Aggregate tool listing from all servers
    std::vector<McpToolDef> list_all_tools();

    // List tools from a specific server
    std::vector<McpToolDef> list_tools(const std::string& server_name) const;

    // Get a connected client by server name (nullptr if not connected)
    McpClient* get_client(const std::string& server_name) const;

    // Route tool call to appropriate server
    McpToolResult call_tool(const std::string& tool_name, const json& arguments);

    // Get server for a tool
    std::string tool_server(const std::string& tool_name);

    // Load config from file
    static std::vector<McpServerConfig> load_config(const std::string& path);

    // Get tool definition by name
    std::optional<McpToolDef> get_tool(const std::string& name);

    // Status
    bool is_server_connected(const std::string& name);
    size_t connected_count();

private:
    // Internal helper
    bool connect_server_unlocked(const std::string& name);

    std::map<std::string, McpServerConfig> configs_;
    std::map<std::string, std::unique_ptr<McpClient>> clients_;
    std::map<std::string, std::vector<McpToolDef>> server_tools_;
    mutable std::recursive_mutex mutex_;
};

}  // namespace mcp
}  // namespace claude
