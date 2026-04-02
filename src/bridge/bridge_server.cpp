#include "bridge/bridge_server.h"
#include "util/string_utils.h"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <chrono>
#include <algorithm>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <thread>
#include <vector>
#include <openssl/sha.h>

namespace claude {

using json = nlohmann::json;

// ============================================================
// BridgeServer — HTTP-based IDE bridge
// ============================================================

BridgeServer::~BridgeServer() { stop(); }

bool BridgeServer::start(int port) {
    if (running_) return true;

    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        spdlog::error("Bridge: socket() failed: {}", strerror(errno));
        return false;
    }

    // Allow address reuse
    int opt = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(listen_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        spdlog::error("Bridge: bind() failed: {}", strerror(errno));
        close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    if (listen(listen_fd_, 16) < 0) {
        spdlog::error("Bridge: listen() failed: {}", strerror(errno));
        close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    // Get actual port (if 0 was specified)
    socklen_t len = sizeof(addr);
    getsockname(listen_fd_, (struct sockaddr*)&addr, &len);
    port_ = ntohs(addr.sin_port);

    running_ = true;
    accept_thread_ = std::thread(&BridgeServer::accept_loop, this);

    spdlog::info("IDE Bridge server started on port {}", port_);
    return true;
}

void BridgeServer::stop() {
    if (!running_) return;
    running_ = false;

    if (listen_fd_ >= 0) {
        shutdown(listen_fd_, SHUT_RDWR);
        close(listen_fd_);
        listen_fd_ = -1;
    }

    // Close all client connections
    std::lock_guard<std::mutex> lock(mutex_);
    clients_.clear();

    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }

    spdlog::info("IDE Bridge server stopped");
}

std::string BridgeServer::connection_url() const {
    return "http://localhost:" + std::to_string(port_);
}

void BridgeServer::broadcast(const BridgeMessage& msg) {
    std::lock_guard<std::mutex> lock(mutex_);

    json payload;
    payload["id"] = msg.id;
    payload["method"] = msg.method;
    if (!msg.data.empty()) {
        try { payload["data"] = json::parse(msg.data); } catch (...) { payload["data"] = msg.data; }
    }
    if (!msg.params.is_null()) {
        payload["params"] = msg.params;
    }

    std::string body = payload.dump();
    std::string response =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: " + std::to_string(body.size()) + "\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "\r\n" + body;

    spdlog::debug("Bridge broadcast: {} to {} clients", msg.method, clients_.size());
}

void BridgeServer::send(const std::string& /*client_id*/, const BridgeMessage& /*msg*/) {
    // Send to a specific client via its socket
    std::lock_guard<std::mutex> lock(mutex_);
    // TODO: find client and send via fd or websocket
}

void BridgeServer::on_message(MessageHandler handler) {
    message_handler_ = std::move(handler);
}

std::vector<BridgeClient> BridgeServer::clients() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<BridgeClient> result;
    for (const auto& [_, client] : clients_) result.push_back(client);
    return result;
}

void BridgeServer::accept_loop() {
    while (running_) {
        struct pollfd pfd;
        pfd.fd = listen_fd_;
        pfd.events = POLLIN;
        pfd.revents = 0;

        int ret = poll(&pfd, 1, 500);  // 500ms timeout
        if (ret <= 0) continue;

        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) break;
        if (!(pfd.revents & POLLIN)) continue;

        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(listen_fd_, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) continue;

        handle_client(client_fd);
    }
}

void BridgeServer::handle_client(int client_fd) {
    // Read the HTTP request
    char buf[4096];
    std::string request;

    // Read until we have the full header
    while (running_) {
        ssize_t n = ::recv(client_fd, buf, sizeof(buf) - 1, MSG_NOSIGNAL);
        if (n <= 0) break;
        buf[n] = '\0';
        request.append(buf);

        // Check if headers are complete (ends with \r\n\r\n)
        if (request.find("\r\n\r\n") != std::string::npos) break;
    }

    if (request.empty()) {
        close(client_fd);
        return;
    }

    // Split headers and body
    size_t header_end = request.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        close(client_fd);
        return;
    }
    std::string headers_part = request.substr(0, header_end);
    std::string body = request.substr(header_end + 4);

    // Check Content-Length header
    int content_length = 0;
    auto cl_pos = headers_part.find("Content-Length:");
    if (cl_pos == std::string::npos) cl_pos = headers_part.find("content-length:");
    if (cl_pos != std::string::npos) {
        auto cl_end = headers_part.find("\r\n", cl_pos);
        std::string cl_str = headers_part.substr(cl_pos + 15, cl_end - cl_pos - 15);
        // Trim whitespace
        while (!cl_str.empty() && (cl_str[0] == ' ' || cl_str[0] == '\t')) cl_str = cl_str.substr(1);
        while (!cl_str.empty() && (cl_str.back() == ' ' || cl_str.back() == '\t')) cl_str.pop_back();
        try { content_length = std::stoi(cl_str); } catch (...) { content_length = 0; }
    }

    // Read remaining body bytes if needed
    while (running_ && static_cast<int>(body.size()) < content_length) {
        ssize_t n = ::recv(client_fd, buf, sizeof(buf) - 1, MSG_NOSIGNAL);
        if (n <= 0) break;
        buf[n] = '\0';
        body.append(buf);
    }

    // Parse HTTP method and path
    std::string method;
    std::string path;

    auto first_line = request.substr(0, request.find("\r\n"));
    auto space1 = first_line.find(' ');
    auto space2 = first_line.find(' ', space1 + 1);
    if (space1 != std::string::npos && space2 != std::string::npos) {
        method = first_line.substr(0, space1);
        path = first_line.substr(space1 + 1, space2 - space1 - 1);
    }

    // Route the request
    json response_json;
    int http_status = 200;

    if (path == "/health" || path == "/") {
        response_json["status"] = "ok";
        response_json["version"] = "0.1.0";
        response_json["connections"] = static_cast<int>(clients_.size());
    }
    else if (path == "/connect" && method == "POST") {
        try {
            auto j = json::parse(body);
            BridgeClient client;
            client.id = util::generate_uuid();
            client.name = j.value("name", "unknown");
            client.version = j.value("version", "0.0.0");
            client.session_id = j.value("session_id", "");
            client.connected_at = std::chrono::system_clock::now();

            {
                std::lock_guard<std::mutex> lock(mutex_);
                clients_[client.id] = client;
            }

            response_json["client_id"] = client.id;
            response_json["status"] = "connected";
            spdlog::info("Bridge client connected: {} ({})", client.name, client.id);
        } catch (const std::exception& e) {
            http_status = 400;
            response_json["error"] = e.what();
        }
    }
    else if (path == "/disconnect" && method == "POST") {
        try {
            auto j = json::parse(body);
            std::string client_id = j.value("client_id", "");
            {
                std::lock_guard<std::mutex> lock(mutex_);
                clients_.erase(client_id);
            }
            response_json["status"] = "disconnected";
            spdlog::info("Bridge client disconnected: {}", client_id);
        } catch (const std::exception& e) {
            http_status = 400;
            response_json["error"] = e.what();
        }
    }
    else if (path == "/message" && method == "POST") {
        try {
            auto j = json::parse(body);
            BridgeMessage msg;
            msg.id = j.value("id", util::generate_uuid());
            msg.method = j.value("method", "");
            msg.data = j.value("data", "");

            // Find client
            std::string client_id = j.value("client_id", "");
            BridgeClient* client_ptr = nullptr;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                auto it = clients_.find(client_id);
                if (it != clients_.end()) client_ptr = &it->second;
            }

            if (client_ptr && message_handler_) {
                message_handler_(*client_ptr, msg);
                response_json["status"] = "ok";
            } else {
                http_status = 404;
                response_json["error"] = "Client not found or no handler";
            }
        } catch (const std::exception& e) {
            http_status = 400;
            response_json["error"] = e.what();
        }
    }
    else if (path == "/sessions") {
        response_json["sessions"] = json::array();
        // Could list sessions from SessionManager here
    }
    else {
        http_status = 404;
        response_json["error"] = "Not found";
    }

    // Send response
    std::string body_str = response_json.dump();
    std::string status_text = (http_status == 200) ? "OK" : "Error";

    std::string response =
        "HTTP/1.1 " + std::to_string(http_status) + " " + status_text + "\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: " + std::to_string(body_str.size()) + "\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type\r\n"
        "\r\n" + body_str;

    ::send(client_fd, response.data(), response.size(), 0);

    // Check if WebSocket upgrade requested
    if (path == "/ws" && method == "GET") {
        auto upgrade = headers_part.find("Upgrade:");
        auto key_line = headers_part.find("Sec-WebSocket-Key:");
        if (upgrade != std::string::npos && key_line != std::string::npos) {
            auto key_end = headers_part.find("\r\n", key_line);
            std::string sec_key = headers_part.substr(key_line + 19, key_end - key_line - 19);
            // Trim whitespace
            while (!sec_key.empty() && (sec_key.back() == '\r' || sec_key.back() == ' ')) sec_key.pop_back();
            handle_websocket(client_fd, sec_key);
            return;  // handle_websocket owns the fd now
        }
    }

    close(client_fd);
}

// ============================================================
// WebSocket Support
// ============================================================
// WebSocket Support
// ============================================================

void BridgeServer::handle_websocket(int fd, const std::string& sec_key) {
    // Send WebSocket upgrade response
    std::string accept_key = websocket_accept_key(sec_key);
    std::string upgrade_response =
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: " + accept_key + "\r\n"
        "\r\n";

    ssize_t n = ::send(fd, upgrade_response.data(), upgrade_response.size(), MSG_NOSIGNAL);
    if (n != static_cast<ssize_t>(upgrade_response.size())) {
        spdlog::error("Bridge WS: Failed to send upgrade response");
        close(fd);
        return;
    }

    spdlog::info("Bridge WS: WebSocket connection established");

    // Register as WebSocket client
    std::string client_id;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        BridgeClient client;
        client.id = util::generate_uuid();
        client.name = "websocket";
        client.version = "ws";
        client.connected_at = std::chrono::system_clock::now();
        client.fd = fd;
        client.websocket = true;
        clients_[client.id] = client;
        client_id = client.id;
    }

    // WebSocket message loop
    while (running_) {
        std::string frame = websocket_recv_frame(fd);
        if (frame.empty()) break;

        // Parse as JSON message
        try {
            auto j = nlohmann::json::parse(frame);
            BridgeMessage msg;
            msg.id = j.value("id", util::generate_uuid());
            msg.method = j.value("method", "");
            msg.data = j.value("data", "");

            if (msg.method == "ping") {
                websocket_send_frame(fd, R"({"type":"pong"})", false);
                continue;
            }

            if (!msg.method.empty() && message_handler_) {
                BridgeClient client_info;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    auto it = clients_.find(client_id);
                    if (it != clients_.end()) client_info = it->second;
                }
                message_handler_(client_info, msg);
            }

            // Send acknowledgment
            json ack;
            ack["type"] = "ack";
            ack["id"] = msg.id;
            websocket_send_frame(fd, ack.dump(), false);

        } catch (const std::exception& e) {
            spdlog::debug("Bridge WS: Parse error: {}", e.what());
        }
    }

    // Cleanup
    {
        std::lock_guard<std::mutex> lock(mutex_);
        clients_.erase(client_id);
    }
    close(fd);
    spdlog::info("Bridge WS: Client disconnected");
}

std::string BridgeServer::websocket_accept_key(const std::string& client_key) {
    // Concatenate client key with the WebSocket GUID
    std::string combined = client_key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

    // SHA-1 hash
    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1(reinterpret_cast<const unsigned char*>(combined.data()), combined.size(), hash);

    // Base64 encode
    return encode_base64(std::string(reinterpret_cast<const char*>(hash), SHA_DIGEST_LENGTH));
}

bool BridgeServer::websocket_send_frame(int fd, const std::string& payload, bool binary) {
    std::vector<uint8_t> frame;

    // Fin + opcode (0x01=text, 0x02=binary)
    uint8_t opcode = binary ? 0x82 : 0x81;
    frame.push_back(opcode);

    size_t len = payload.size();
    if (len < 126) {
        frame.push_back(static_cast<uint8_t>(len));
    } else if (len < 65536) {
        frame.push_back(126);
        frame.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
        frame.push_back(static_cast<uint8_t>(len & 0xFF));
    } else {
        frame.push_back(127);
        for (int i = 56; i >= 0; i -= 8) {
            frame.push_back(static_cast<uint8_t>((len >> i) & 0xFF));
        }
    }

    // Payload
    frame.insert(frame.end(), payload.begin(), payload.end());

    ssize_t n = ::send(fd, frame.data(), frame.size(), MSG_NOSIGNAL);
    return n == static_cast<ssize_t>(frame.size());
}

std::string BridgeServer::websocket_recv_frame(int fd) {
    uint8_t header[2];
    ssize_t n = ::recv(fd, header, 2, MSG_NOSIGNAL);
    if (n < 2) return "";

    // bool fin = (header[0] & 0x80) != 0;  // Used for fragmentation (not implemented)
    uint8_t opcode = header[0] & 0x0F;
    bool masked = (header[1] & 0x80) != 0;
    uint64_t payload_len = header[1] & 0x7F;

    // Handle close frame
    if (opcode == 0x08) {
        // Send close frame back
        uint8_t close_frame[2] = {0x88, 0x00};
        ::send(fd, close_frame, 2, MSG_NOSIGNAL);
        return "";
    }

    // Handle ping
    if (opcode == 0x09) {
        std::vector<uint8_t> pong_frame;
        if (payload_len > 0) {
            std::vector<uint8_t> payload_data(payload_len);
            ::recv(fd, payload_data.data(), payload_len, MSG_NOSIGNAL);
            pong_frame.push_back(0x8A);
            pong_frame.push_back(static_cast<uint8_t>(payload_len));
            pong_frame.insert(pong_frame.end(), payload_data.begin(), payload_data.end());
        } else {
            pong_frame = {0x8A, 0x00};
        }
        ::send(fd, pong_frame.data(), pong_frame.size(), MSG_NOSIGNAL);
        return websocket_recv_frame(fd);  // Recurse for next frame
    }

    // Extended payload length
    if (payload_len == 126) {
        uint8_t ext[2];
        if (::recv(fd, ext, 2, MSG_NOSIGNAL) != 2) return "";
        payload_len = (static_cast<uint64_t>(ext[0]) << 8) | ext[1];
    } else if (payload_len == 127) {
        uint8_t ext[8];
        if (::recv(fd, ext, 8, MSG_NOSIGNAL) != 8) return "";
        payload_len = 0;
        for (int i = 0; i < 8; i++) {
            payload_len = (payload_len << 8) | ext[i];
        }
    }

    // Read mask key if present
    uint8_t mask_key[4] = {};
    if (masked) {
        if (::recv(fd, mask_key, 4, MSG_NOSIGNAL) != 4) return "";
    }

    // Read payload
    std::string payload(payload_len, '\0');
    size_t total_read = 0;
    while (total_read < payload_len) {
        ssize_t r = ::recv(fd, &payload[total_read], payload_len - total_read, MSG_NOSIGNAL);
        if (r <= 0) return "";
        total_read += r;
    }

    // Unmask if needed
    if (masked) {
        for (size_t i = 0; i < payload_len; i++) {
            payload[i] ^= mask_key[i % 4];
        }
    }

    return payload;
}

std::string BridgeServer::decode_base64(const std::string& encoded) {
    static const std::string chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    std::vector<int> T(256, -1);
    for (int i = 0; i < 64; i++) T[static_cast<unsigned char>(chars[i])] = i;

    int val = 0, valb = -8;
    for (unsigned char c : encoded) {
        if (T[c] == -1) break;
        val = (val << 6) + T[c];
        valb += 6;
        if (valb >= 0) {
            result.push_back(static_cast<char>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return result;
}

std::string BridgeServer::encode_base64(const std::string& data) {
    static const std::string chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string result;
    int b;
    for (size_t i = 0; i < data.size(); i += 3) {
        b = (static_cast<unsigned char>(data[i]) & 0xFC) >> 2;
        result.push_back(chars[b]);
        b = (static_cast<unsigned char>(data[i]) & 0x03) << 4;
        if (i + 1 < data.size()) {
            b |= (static_cast<unsigned char>(data[i+1]) & 0xF0) >> 4;
            result.push_back(chars[b]);
            b = (static_cast<unsigned char>(data[i+1]) & 0x0F) << 2;
            if (i + 2 < data.size()) {
                b |= (static_cast<unsigned char>(data[i+2]) & 0xC0) >> 6;
                result.push_back(chars[b]);
                b = static_cast<unsigned char>(data[i+2]) & 0x3F;
                result.push_back(chars[b]);
            } else {
                result.push_back(chars[b]);
                result.push_back('=');
            }
        } else {
            result.push_back(chars[b]);
            result.push_back('=');
            result.push_back('=');
        }
    }
    return result;
}

// ============================================================
// SessionManager
// ============================================================

std::string SessionManager::create_session(const std::string& client_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    Session session;
    session.id = util::generate_uuid();
    session.created_at = util::get_iso_date();

    if (!client_id.empty()) {
        session.client_ids.push_back(client_id);
        client_to_session_[client_id] = session.id;
    }

    sessions_[session.id] = session;
    spdlog::debug("Session created: {} (client: {})", session.id, client_id);
    return session.id;
}

std::string SessionManager::get_session(const std::string& session_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(session_id);
    if (it != sessions_.end()) {
        return json{
            {"id", it->second.id},
            {"clients", it->second.client_ids},
            {"created_at", it->second.created_at},
            {"working_directory", it->second.working_directory}
        }.dump();
    }
    return "{}";
}

std::vector<std::string> SessionManager::list_sessions() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> result;
    for (const auto& [id, _] : sessions_) result.push_back(id);
    return result;
}

void SessionManager::attach(const std::string& session_id, const std::string& client_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(session_id);
    if (it != sessions_.end()) {
        it->second.client_ids.push_back(client_id);
        client_to_session_[client_id] = session_id;
    }
}

void SessionManager::detach(const std::string& client_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = client_to_session_.find(client_id);
    if (it != client_to_session_.end()) {
        auto sit = sessions_.find(it->second);
        if (sit != sessions_.end()) {
            auto& cls = sit->second.client_ids;
            cls.erase(std::remove(cls.begin(), cls.end(), client_id), cls.end());
        }
        client_to_session_.erase(it);
    }
}

void SessionManager::destroy_session(const std::string& session_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(session_id);
    if (it != sessions_.end()) {
        for (const auto& cid : it->second.client_ids) {
            client_to_session_.erase(cid);
        }
        sessions_.erase(it);
    }
}

}  // namespace claude
