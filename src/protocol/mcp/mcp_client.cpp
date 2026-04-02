#include "protocol/mcp/mcp_client.h"
#include "util/process_utils.h"
#include "util/string_utils.h"
#include <spdlog/spdlog.h>
#include <sstream>
#include <algorithm>
#include <fstream>
#include <sys/wait.h>
#include <unistd.h>

namespace claude {
namespace mcp {

// ============================================================
// JsonRpcMessage
// ============================================================

std::string JsonRpcMessage::to_string() const {
    json j;
    j["jsonrpc"] = "2.0";
    if (!id.empty()) j["id"] = id;
    if (!method.empty()) j["method"] = method;
    if (!params.is_null()) j["params"] = params;
    if (!result.is_null()) j["result"] = result;
    if (!error.is_null()) j["error"] = error;
    return j.dump();
}

JsonRpcMessage JsonRpcMessage::parse(const std::string& raw) {
    JsonRpcMessage msg;
    try {
        auto j = json::parse(raw);
        msg.jsonrpc = j.value("jsonrpc", "2.0");
        msg.id = j.value("id", "");
        msg.method = j.value("method", "");
        if (j.contains("params")) msg.params = j["params"];
        if (j.contains("result")) msg.result = j["result"];
        if (j.contains("error")) msg.error = j["error"];
    } catch (const json::parse_error& e) {
        spdlog::warn("MCP JSON parse error: {}", e.what());
    }
    return msg;
}

// ============================================================
// McpClient
// ============================================================

McpClient::McpClient(const McpServerConfig& config) : config_(config) {}

McpClient::~McpClient() { disconnect(); }

bool McpClient::connect() {
    if (connected_) return true;

    if (config_.transport == "sse" && !config_.url.empty()) {
        return connect_sse();
    }
    return connect_stdio();
}

void McpClient::disconnect() {
    if (!connected_) return;

    if (child_pid_ > 0) {
        kill(child_pid_, SIGTERM);
        // Grace period
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(3000);
        while (std::chrono::steady_clock::now() < deadline) {
            int status;
            pid_t ret = waitpid(child_pid_, &status, WNOHANG);
            if (ret == child_pid_ || ret == -1) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        // Force kill if still running
        kill(child_pid_, SIGKILL);
        waitpid(child_pid_, nullptr, 0);
        child_pid_ = -1;
    }

    if (write_fd_ >= 0) { close(write_fd_); write_fd_ = -1; }
    if (read_fd_ >= 0) { close(read_fd_); read_fd_ = -1; }

    connected_ = false;
    spdlog::info("MCP server '{}' disconnected", config_.name);
}

bool McpClient::connect_stdio() {
    int stdin_pipe[2] = {-1, -1};
    int stdout_pipe[2] = {-1, -1};

    if (pipe(stdin_pipe) != 0 || pipe(stdout_pipe) != 0) {
        spdlog::error("MCP: Failed to create pipes for {}", config_.name);
        return false;
    }

    pid_t pid = fork();
    if (pid < 0) {
        spdlog::error("MCP: Fork failed for {}", config_.name);
        return false;
    }

    if (pid == 0) {
        // Child process
        dup2(stdin_pipe[0], STDIN_FILENO);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        close(stdin_pipe[0]); close(stdin_pipe[1]);
        close(stdout_pipe[0]); close(stdout_pipe[1]);

        // Build argv
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(config_.command.c_str()));
        for (const auto& arg : config_.args) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);

        // Set environment
        for (const auto& [k, v] : config_.env) {
            setenv(k.c_str(), v.c_str(), 1);
        }

        execvp(config_.command.c_str(), argv.data());
        _exit(127);
    }

    // Parent
    close(stdin_pipe[0]);
    close(stdout_pipe[1]);
    read_fd_ = stdout_pipe[0];
    write_fd_ = stdin_pipe[1];
    child_pid_ = pid;

    // Wait a moment for the server to start
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Send initialize request
    auto init_result = send_request("initialize", {
        {"protocolVersion", "2024-11-05"},
        {"capabilities", {}},
        {"clientInfo", {{"name", "claude-code-cpp"}, {"version", "0.1.0"}}}
    });

    if (init_result.result.is_null() && !init_result.error.is_null()) {
        spdlog::error("MCP initialize failed for {}: {}", config_.name, init_result.error.dump());
        disconnect();
        return false;
    }

    if (init_result.result.contains("serverInfo")) {
        server_name_ = init_result.result["serverInfo"].value("name", "");
        server_version_ = init_result.result["serverInfo"].value("version", "");
    }

    // Send initialized notification
    send_notification("notifications/initialized");

    connected_ = true;
    spdlog::info("MCP server '{}' connected ({} v{})", config_.name, server_name_, server_version_);
    return true;
}

bool McpClient::connect_sse() {
    // SSE transport stub - will use libcurl in full implementation
    spdlog::warn("MCP SSE transport not fully implemented for {}", config_.name);
    return false;
}

void McpClient::write_message(const std::string& message) {
    if (write_fd_ < 0) return;
    std::string framed = "Content-Length: " + std::to_string(message.size()) + "\r\n\r\n" + message;
    ssize_t n = write(write_fd_, framed.data(), framed.size());
    if (n < 0) {
        spdlog::error("MCP write error for {}", config_.name);
    }
}

std::string McpClient::read_response(int timeout_ms) {
    if (read_fd_ < 0) return "";

    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    std::string buffer;

    while (std::chrono::steady_clock::now() < deadline) {
        char buf[4096];
        ssize_t n = read(read_fd_, buf, sizeof(buf));
        if (n <= 0) {
            if (n == 0) return "";  // EOF
            if (errno == EINTR) continue;
            return "";
        }
        buffer.append(buf, n);

        // Try to parse Content-Length framed messages
        // Format: "Content-Length: <len>\r\n\r\n<body>"
        size_t header_end = buffer.find("\r\n\r\n");
        if (header_end != std::string::npos) {
            size_t content_start = header_end + 4;
            // Find Content-Length
            size_t cl_pos = buffer.find("Content-Length: ");
            if (cl_pos != std::string::npos && cl_pos < header_end) {
                size_t cl_end = buffer.find("\r\n", cl_pos);
                int content_length = std::stoi(buffer.substr(cl_pos + 16, cl_end - cl_pos - 16));
                if (static_cast<int>(buffer.size() - content_start) >= content_length) {
                    return buffer.substr(content_start, content_length);
                }
            }
        }
    }

    return buffer;  // Return whatever we have
}

JsonRpcMessage McpClient::send_request(const std::string& method, const json& params) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::string id = std::to_string(next_id_++);
    JsonRpcMessage msg;
    msg.id = id;
    msg.method = method;
    msg.params = params;

    write_message(msg.to_string());
    std::string response = read_response(config_.timeout_seconds * 1000);

    if (response.empty()) {
        return msg;  // Empty result
    }

    return JsonRpcMessage::parse(response);
}

void McpClient::send_notification(const std::string& method, const json& params) {
    JsonRpcMessage msg;
    msg.method = method;
    msg.params = params;
    write_message(msg.to_string());
}

void McpClient::process_message(const JsonRpcMessage& /*msg*/) {
    // Process incoming notifications (tool progress, etc.)
    // Stub for P3
}

std::vector<McpToolDef> McpClient::list_tools() {
    if (!connected_) return {};

    auto result = send_request("tools/list", {});

    std::vector<McpToolDef> tools;
    if (result.result.contains("tools") && result.result["tools"].is_array()) {
        for (const auto& t : result.result["tools"]) {
            McpToolDef tool;
            tool.name = t.value("name", "");
            tool.description = t.value("description", "");
            if (t.contains("inputSchema")) tool.input_schema = t["inputSchema"];
            if (!tool.name.empty()) tools.push_back(tool);
        }
    }

    spdlog::debug("MCP server '{}' listed {} tools", config_.name, tools.size());
    return tools;
}

McpToolResult McpClient::call_tool(const std::string& name, const json& arguments) {
    McpToolResult result;

    if (!connected_) {
        result.error_message = "Server not connected";
        return result;
    }

    auto response = send_request("tools/call", {
        {"name", name},
        {"arguments", arguments.is_null() ? json::object() : arguments}
    });

    if (!response.error.is_null()) {
        result.is_error = true;
        result.error_message = response.error.value("message", "Unknown error");
        return result;
    }

    if (response.result.contains("content")) {
        result.content = response.result["content"];
        result.success = true;
    }

    result.is_error = response.result.value("isError", false);
    return result;
}

std::vector<McpResource> McpClient::list_resources() {
    if (!connected_) return {};

    auto result = send_request("resources/list", {});

    std::vector<McpResource> resources;
    if (result.result.contains("resources") && result.result["resources"].is_array()) {
        for (const auto& r : result.result["resources"]) {
            McpResource res;
            res.uri = r.value("uri", "");
            res.name = r.value("name", "");
            res.description = r.value("description", "");
            res.mime_type = r.value("mimeType", "");
            if (!res.uri.empty()) resources.push_back(res);
        }
    }
    return resources;
}

std::optional<std::string> McpClient::read_resource(const std::string& uri) {
    if (!connected_) return std::nullopt;

    auto result = send_request("resources/read", {{"uri", uri}});

    if (result.result.contains("contents") && result.result["contents"].is_array() &&
        !result.result["contents"].empty()) {
        auto& content = result.result["contents"][0];
        return content.value("text", "");
    }
    return std::nullopt;
}

json McpClient::list_prompts() {
    if (!connected_) return json::array();
    auto result = send_request("prompts/list", {});
    return result.result.value("prompts", json::array());
}

json McpClient::get_prompt(const std::string& name, const json& arguments) {
    if (!connected_) return json::object();
    auto result = send_request("prompts/get", {{"name", name}, {"arguments", arguments}});
    return result.result;
}

// ============================================================
// McpManager
// ============================================================

McpManager::~McpManager() { disconnect_all(); }

void McpManager::add_server(const McpServerConfig& config) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    configs_[config.name] = config;
}

bool McpManager::remove_server(const std::string& name) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    disconnect_server(name);
    configs_.erase(name);
    clients_.erase(name);
    server_tools_.erase(name);
    return true;
}

std::vector<std::string> McpManager::server_names() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::vector<std::string> names;
    for (const auto& [name, _] : configs_) names.push_back(name);
    return names;
}

bool McpManager::connect_all() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    bool all_ok = true;
    for (const auto& [name, config] : configs_) {
        if (!config.enabled) continue;
        if (!connect_server_unlocked(name)) all_ok = false;
    }
    return all_ok;
}

void McpManager::disconnect_all() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    for (auto& [name, client] : clients_) {
        if (client) client->disconnect();
    }
    clients_.clear();
    server_tools_.clear();
}

bool McpManager::connect_server(const std::string& name) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return connect_server_unlocked(name);
}

bool McpManager::connect_server_unlocked(const std::string& name) {
    auto it = configs_.find(name);
    if (it == configs_.end()) return false;

    auto client = std::make_unique<McpClient>(it->second);
    if (client->connect()) {
        clients_[name] = std::move(client);
        server_tools_[name] = clients_[name]->list_tools();
        return true;
    }
    return false;
}

void McpManager::disconnect_server(const std::string& name) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = clients_.find(name);
    if (it != clients_.end()) {
        it->second->disconnect();
        clients_.erase(it);
    }
    server_tools_.erase(name);
}

std::vector<McpToolDef> McpManager::list_all_tools() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::vector<McpToolDef> all;
    for (const auto& [server, tools] : server_tools_) {
        for (const auto& tool : tools) {
            all.push_back(tool);
        }
    }
    return all;
}

std::vector<McpToolDef> McpManager::list_tools(const std::string& server_name) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = server_tools_.find(server_name);
    if (it != server_tools_.end()) return it->second;
    return {};
}

McpClient* McpManager::get_client(const std::string& server_name) const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = clients_.find(server_name);
    if (it != clients_.end()) return it->second.get();
    return nullptr;
}

McpToolResult McpManager::call_tool(const std::string& tool_name, const json& arguments) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    for (auto& [name, tools] : server_tools_) {
        for (const auto& t : tools) {
            if (t.name == tool_name) {
                auto it = clients_.find(name);
                if (it != clients_.end() && it->second) {
                    return it->second->call_tool(tool_name, arguments);
                }
            }
        }
    }
    McpToolResult result;
    result.error_message = "Tool not found: " + tool_name;
    return result;
}

std::string McpManager::tool_server(const std::string& tool_name) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    for (const auto& [server, tools] : server_tools_) {
        for (const auto& t : tools) {
            if (t.name == tool_name) return server;
        }
    }
    return "";
}

std::optional<McpToolDef> McpManager::get_tool(const std::string& name) {
    auto all = list_all_tools();
    for (const auto& t : all) {
        if (t.name == name) return t;
    }
    return std::nullopt;
}

bool McpManager::is_server_connected(const std::string& name) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    auto it = clients_.find(name);
    return it != clients_.end() && it->second && it->second->is_connected();
}

size_t McpManager::connected_count() {
    size_t count = 0;
    for (const auto& [_, client] : clients_) {
        if (client && client->is_connected()) count++;
    }
    return count;
}

std::vector<McpServerConfig> McpManager::load_config(const std::string& path) {
    std::vector<McpServerConfig> servers;
    std::ifstream f(path);
    if (!f.is_open()) return servers;

    try {
        json j;
        f >> j;
        if (j.contains("mcpServers") && j["mcpServers"].is_object()) {
            for (const auto& [name, config] : j["mcpServers"].items()) {
                McpServerConfig server;
                server.name = name;
                server.command = config.value("command", "");
                server.url = config.value("url", "");

                if (config.contains("args") && config["args"].is_array()) {
                    for (const auto& arg : config["args"]) {
                        server.args.push_back(arg.get<std::string>());
                    }
                }

                if (config.contains("env") && config["env"].is_object()) {
                    for (const auto& [k, v] : config["env"].items()) {
                        server.env[k] = v.get<std::string>();
                    }
                }

                if (!server.url.empty()) server.transport = "sse";

                servers.push_back(server);
            }
        }
    } catch (const std::exception& e) {
        spdlog::warn("MCP config parse error: {}", e.what());
    }

    return servers;
}

}  // namespace mcp
}  // namespace claude
