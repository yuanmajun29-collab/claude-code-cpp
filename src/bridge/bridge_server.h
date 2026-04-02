#pragma once

#include <string>
#include <map>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <atomic>
#include <nlohmann/json.hpp>

namespace claude {

// IDE bridge — connects Claude Code C++ to IDEs (VS Code, JetBrains, etc.)
// Uses a simple WebSocket/HTTP protocol for communication

struct BridgeMessage {
    std::string id;
    std::string method;  // "tool_call", "response", "status", "notification"
    nlohmann::json params;
    std::string data;
};

struct BridgeClient {
    std::string id;
    std::string name;  // "vscode", "jetbrains", "emacs", etc.
    std::string version;
    std::string session_id;
    std::chrono::system_clock::time_point connected_at;
    int fd = -1;           // Socket file descriptor (-1 if not connected)
    bool websocket = false; // Whether using WebSocket protocol
};

class BridgeServer {
public:
    BridgeServer() = default;
    ~BridgeServer();

    // Start/stop server
    bool start(int port = 0);
    void stop();
    bool is_running() const { return running_; }

    // Get port
    int port() const { return port_; }

    // Get URL for client connection
    std::string connection_url() const;

    // Send message to all clients
    void broadcast(const BridgeMessage& msg);

    // Send to specific client
    void send(const std::string& client_id, const BridgeMessage& msg);

    // Register message handler
    using MessageHandler = std::function<void(const BridgeClient&, const BridgeMessage&)>;
    void on_message(MessageHandler handler);

    // Get connected clients
    std::vector<BridgeClient> clients() const;

private:
    void accept_loop();
    void handle_client(int fd);

    // WebSocket support
    void handle_websocket(int fd, const std::string& sec_key);
    std::string websocket_accept_key(const std::string& client_key);
    bool websocket_send_frame(int fd, const std::string& payload, bool binary = false);
    std::string websocket_recv_frame(int fd);
    std::string decode_base64(const std::string& encoded);
    std::string encode_base64(const std::string& data);
    void sha1(const unsigned char* data, size_t len, unsigned char* hash);

    int listen_fd_ = -1;
    int port_ = 0;
    std::atomic<bool> running_{false};
    std::thread accept_thread_;
    std::vector<std::thread> client_threads_;

    std::map<std::string, BridgeClient> clients_;
    mutable std::mutex mutex_;

    MessageHandler message_handler_;
};

// Session manager for bridge
class SessionManager {
public:
    SessionManager() = default;

    // Create new session
    std::string create_session(const std::string& client_id = "");

    // Get session info
    std::string get_session(const std::string& session_id) const;

    // List sessions
    std::vector<std::string> list_sessions() const;

    // Attach client to session
    void attach(const std::string& session_id, const std::string& client_id);

    // Detach client from session
    void detach(const std::string& client_id);

    // Destroy session
    void destroy_session(const std::string& session_id);

private:
    struct Session {
        std::string id;
        std::vector<std::string> client_ids;
        std::string created_at;
        std::string working_directory;
    };

    std::map<std::string, Session> sessions_;
    std::map<std::string, std::string> client_to_session_;  // client_id -> session_id
    mutable std::mutex mutex_;
};

}  // namespace claude
