#pragma once

#include "protocol/mcp/mcp_client.h"
#include <string>
#include <functional>
#include <memory>
#include <thread>
#include <atomic>
#include <mutex>
#include <queue>
#include <nlohmann/json.hpp>

namespace claude {
namespace mcp {

// HTTP-based MCP transport (SSE + HTTP POST)
class McpSseTransport {
public:
    using MessageCallback = std::function<void(const std::string& message)>;
    using ErrorCallback = std::function<void(const std::string& error)>;
    using EventCallback = std::function<void(const std::string& event_type, const std::string& data)>;

    McpSseTransport();
    ~McpSseTransport();

    // Connect to an SSE endpoint
    bool connect(const std::string& url,
                 const std::map<std::string, std::string>& headers = {});

    // Disconnect
    void disconnect();

    // Check if connected
    bool is_connected() const { return connected_; }

    // Send a JSON-RPC message via HTTP POST
    bool send_message(const std::string& message);

    // Get the endpoint URL for POST requests (from SSE endpoint header)
    const std::string& post_endpoint() const { return post_endpoint_; }

    // Set callbacks
    void on_message(MessageCallback cb) { message_callback_ = std::move(cb); }
    void on_error(ErrorCallback cb) { error_callback_ = std::move(cb); }
    void on_event(EventCallback cb) { event_callback_ = std::move(cb); }

    // Get SSE session URL
    const std::string& session_url() const { return session_url_; }

    // Set timeout
    void set_timeout(int seconds) { timeout_seconds_ = seconds; }

private:
    // SSE listener thread
    void sse_listener();

    // Parse SSE event line
    void parse_sse_line(const std::string& line);

    // Complete SSE event processing
    void process_sse_event(const std::string& event_type, const std::string& data);

    // HTTP POST via libcurl
    bool http_post(const std::string& url, const std::string& body,
                   const std::map<std::string, std::string>& headers);

    // HTTP GET for SSE stream
    bool http_get_sse(const std::string& url,
                      const std::map<std::string, std::string>& headers);

    std::string url_;
    std::string session_url_;
    std::string post_endpoint_;
    std::map<std::string, std::string> headers_;

    std::atomic<bool> connected_{false};
    std::atomic<bool> running_{false};

    std::thread listener_thread_;
    std::mutex mutex_;

    MessageCallback message_callback_;
    ErrorCallback error_callback_;
    EventCallback event_callback_;

    int timeout_seconds_ = 30;
};

// Multi-transport MCP connection (supports both stdio and SSE)
class McpConnection {
public:
    enum class Type { Stdio, Sse };

    McpConnection() = default;

    // Factory methods
    static std::unique_ptr<McpConnection> create_stdio(const McpServerConfig& config);
    static std::unique_ptr<McpConnection> create_sse(const std::string& url,
                                     const std::map<std::string, std::string>& headers = {});

    // Send message
    bool send(const std::string& message);

    // Receive message (blocking with timeout)
    std::string receive(int timeout_ms = 30000);

    // Disconnect
    void disconnect();

    // Check if connected
    bool is_connected() const;

    // Get connection type
    Type type() const { return type_; }

    // Get server name
    const std::string& server_name() const { return server_name_; }

private:
    Type type_ = Type::Stdio;
    std::string server_name_;

    // Stdio transport (pipe-based)
    int write_fd_ = -1;
    int read_fd_ = -1;
    pid_t child_pid_ = -1;
    std::string stdio_buffer_;

    // SSE transport
    std::unique_ptr<McpSseTransport> sse_transport_;
    std::queue<std::string> sse_message_queue_;
    mutable std::mutex queue_mutex_;

    // Common: send via appropriate transport
    void stdio_send(const std::string& message);
    void stdio_disconnect();
};

}  // namespace mcp
}  // namespace claude
