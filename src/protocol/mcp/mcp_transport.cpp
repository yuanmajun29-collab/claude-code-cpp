#include "protocol/mcp/mcp_transport.h"
#include "protocol/mcp/mcp_client.h"
#include "util/string_utils.h"
#include <nlohmann/json.hpp>
#include <curl/curl.h>
#include <spdlog/spdlog.h>
#include <sstream>
#include <cstring>
#include <sys/wait.h>
#include <sys/poll.h>
#include <unistd.h>
#include <fcntl.h>

namespace claude {
namespace mcp {

using json = nlohmann::json;

// ============================================================
// Curl helpers
// ============================================================

static size_t curl_write_cb(void* contents, size_t size, size_t nmemb, void* userp) {
    auto* str = static_cast<std::string*>(userp);
    size_t total = size * nmemb;
    str->append(static_cast<char*>(contents), total);
    return total;
}

// Global curl init (thread-safe via static)
static CURL* get_curl_handle() {
    static std::once_flag flag;
    std::call_once(flag, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
    return curl_easy_init();
}

// ============================================================
// McpSseTransport
// ============================================================

McpSseTransport::McpSseTransport() = default;

McpSseTransport::~McpSseTransport() { disconnect(); }

bool McpSseTransport::connect(const std::string& url,
                               const std::map<std::string, std::string>& headers) {
    url_ = url;
    headers_ = headers;

    // Step 1: Connect to SSE endpoint to get session info
    // The server typically returns a "endpoint" event with the POST URL
    if (!http_get_sse(url, headers)) {
        spdlog::error("MCP SSE: Failed to connect to {}", url);
        return false;
    }

    connected_ = true;
    spdlog::info("MCP SSE connected to {} (post: {})", url, post_endpoint_);
    return true;
}

void McpSseTransport::disconnect() {
    if (!connected_) return;
    running_ = false;
    connected_ = false;

    if (listener_thread_.joinable()) {
        listener_thread_.join();
    }

    spdlog::info("MCP SSE disconnected");
}

bool McpSseTransport::send_message(const std::string& message) {
    if (!connected_ || post_endpoint_.empty()) return false;

    std::map<std::string, std::string> headers = {
        {"Content-Type", "application/json"},
        {"Accept", "application/json, text/event-stream"}
    };

    // Copy auth headers from connection
    for (const auto& [k, v] : headers_) {
        if (util::to_lower(k) == "authorization" || util::to_lower(k) == "x-api-key") {
            headers[k] = v;
        }
    }

    return http_post(post_endpoint_, message, headers);
}

void McpSseTransport::sse_listener() {
    // In a full implementation, this would maintain a persistent SSE connection
    // and dispatch events to callbacks.
    // For now, the initial connect already handles the handshake.
}

void McpSseTransport::parse_sse_line(const std::string& line) {
    if (line.empty()) return;

    if (line.substr(0, 6) == "event:") {
        // Event type line — handled by caller
    } else if (line.substr(0, 5) == "data:") {
        std::string data = line.substr(5);
        // Trim leading space
        if (!data.empty() && data[0] == ' ') data = data.substr(1);

        try {
            auto j = json::parse(data);

            // Look for endpoint in SSE data
            if (j.contains("endpoint")) {
                std::string endpoint = j["endpoint"].get<std::string>();
                // Resolve relative URLs
                if (endpoint.starts_with("/")) {
                    // Extract base URL
                    auto base_end = url_.find("://") + 3;
                    auto path_start = url_.find('/', base_end);
                    if (path_start != std::string::npos) {
                        post_endpoint_ = url_.substr(0, path_start) + endpoint;
                    }
                } else {
                    post_endpoint_ = endpoint;
                }
            }

            if (message_callback_) {
                message_callback_(data);
            }
        } catch (const std::exception& e) {
            if (error_callback_) {
                error_callback_("SSE parse error: " + std::string(e.what()));
            }
        }
    } else if (line.substr(0, 3) == "id:") {
        // Server-sent event ID
    } else if (line.substr(0, 7) == "retry:") {
        // Reconnection interval
    }
}

void McpSseTransport::process_sse_event(const std::string& event_type,
                                          const std::string& data) {
    if (event_callback_) {
        event_callback_(event_type, data);
    }
}

bool McpSseTransport::http_post(const std::string& url, const std::string& body,
                                 const std::map<std::string, std::string>& headers) {
    CURL* curl = get_curl_handle();
    if (!curl) return false;

    std::string response;
    struct curl_slist* hdr_list = nullptr;

    for (const auto& [key, val] : headers) {
        hdr_list = curl_slist_append(hdr_list, (key + ": " + val).c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdr_list);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(timeout_seconds_));

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(hdr_list);

    if (res != CURLE_OK) {
        spdlog::error("MCP SSE POST to {} failed: {}", url, curl_easy_strerror(res));
        return false;
    }

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code >= 400) {
        spdlog::error("MCP SSE POST returned HTTP {}", http_code);
        return false;
    }

    return true;
}

bool McpSseTransport::http_get_sse(const std::string& url,
                                    const std::map<std::string, std::string>& headers) {
    CURL* curl = get_curl_handle();
    if (!curl) return false;

    std::string response;
    std::string response_headers;
    struct curl_slist* hdr_list = nullptr;

    hdr_list = curl_slist_append(hdr_list, "Accept: text/event-stream");
    hdr_list = curl_slist_append(hdr_list, "Cache-Control: no-cache");

    for (const auto& [key, val] : headers) {
        hdr_list = curl_slist_append(hdr_list, (key + ": " + val).c_str());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdr_list);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, curl_write_cb);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response_headers);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(timeout_seconds_));

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(hdr_list);

    if (res != CURLE_OK) {
        spdlog::error("MCP SSE GET {} failed: {}", url, curl_easy_strerror(res));
        return false;
    }

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    if (http_code >= 400) {
        spdlog::error("MCP SSE GET returned HTTP {}", http_code);
        return false;
    }

    // Parse the SSE response body
    std::istringstream stream(response);
    std::string line;
    std::string current_event_type;
    std::string current_data;

    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();

        if (line.starts_with("event: ")) {
            current_event_type = line.substr(7);
        } else if (line.starts_with("data: ")) {
            current_data = line.substr(6);
        } else if (line.empty() && !current_data.empty()) {
            parse_sse_line("data: " + current_data);
            process_sse_event(current_event_type, current_data);
            current_event_type.clear();
            current_data.clear();
        } else {
            parse_sse_line(line);
        }
    }

    // Check for session URL in response headers
    // Some MCP servers return the session URL in a header like "Mcp-Session-Id"
    std::istringstream hstream(response_headers);
    while (std::getline(hstream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (util::to_lower(line).find("mcp-session-id") != std::string::npos) {
            auto pos = line.find(':');
            if (pos != std::string::npos) {
                session_url_ = util::trim(line.substr(pos + 1));
            }
        }
    }

    // Default: if no endpoint event was received, use the URL itself as POST endpoint
    if (post_endpoint_.empty()) {
        post_endpoint_ = url;
    }

    return true;
}

// ============================================================
// McpConnection — unified transport
// ============================================================

std::unique_ptr<McpConnection> McpConnection::create_stdio(const McpServerConfig& config) {
    auto conn = std::make_unique<McpConnection>();
    conn->type_ = Type::Stdio;
    conn->server_name_ = config.name;

    int stdin_pipe[2] = {-1, -1};
    int stdout_pipe[2] = {-1, -1};

    if (pipe(stdin_pipe) != 0 || pipe(stdout_pipe) != 0) {
        spdlog::error("MCP: Failed to create pipes for {}", config.name);
        return nullptr;
    }

    pid_t pid = fork();
    if (pid < 0) {
        spdlog::error("MCP: Fork failed for {}", config.name);
        close(stdin_pipe[0]); close(stdin_pipe[1]);
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        return nullptr;
    }

    if (pid == 0) {
        dup2(stdin_pipe[0], STDIN_FILENO);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        close(stdin_pipe[0]); close(stdin_pipe[1]);
        close(stdout_pipe[0]); close(stdout_pipe[1]);

        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(config.command.c_str()));
        for (const auto& arg : config.args) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);

        for (const auto& [k, v] : config.env) {
            setenv(k.c_str(), v.c_str(), 1);
        }

        execvp(config.command.c_str(), argv.data());
        _exit(127);
    }

    close(stdin_pipe[0]);
    close(stdout_pipe[1]);
    conn->write_fd_ = stdin_pipe[1];
    conn->read_fd_ = stdout_pipe[0];
    conn->child_pid_ = pid;

    return conn;
}

std::unique_ptr<McpConnection> McpConnection::create_sse(const std::string& url,
                                         const std::map<std::string, std::string>& headers) {
    auto conn = std::make_unique<McpConnection>();
    conn->type_ = Type::Sse;
    conn->server_name_ = url;

    auto transport = std::make_unique<McpSseTransport>();
    if (transport->connect(url, headers)) {
        conn->sse_transport_ = std::move(transport);
    } else {
        spdlog::error("MCP SSE: Failed to create connection to {}", url);
    }

    return conn;
}

bool McpConnection::send(const std::string& message) {
    if (type_ == Type::Sse) {
        return sse_transport_ && sse_transport_->send_message(message);
    }

    // Stdio: Content-Length framed
    if (write_fd_ < 0) return false;

    std::string framed = "Content-Length: " + std::to_string(message.size()) + "\r\n\r\n" + message;
    ssize_t n = write(write_fd_, framed.data(), framed.size());
    return n == static_cast<ssize_t>(framed.size());
}

std::string McpConnection::receive(int timeout_ms) {
    if (type_ == Type::Sse) {
        // SSE messages come via callbacks; return from queue
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (!sse_message_queue_.empty()) {
            std::string msg = sse_message_queue_.front();
            sse_message_queue_.pop();
            return msg;
        }
        return "";
    }

    // Stdio: read Content-Length framed
    if (read_fd_ < 0) return "";

    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

    // Set non-blocking for timeout support
    int flags = fcntl(read_fd_, F_GETFL);
    fcntl(read_fd_, F_SETFL, flags | O_NONBLOCK);

    // Read until we have a complete message
    while (std::chrono::steady_clock::now() < deadline) {
        char buf[4096];

        // Use poll for timeout
        struct pollfd pfd;
        pfd.fd = read_fd_;
        pfd.events = POLLIN;
        int remaining_ms = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count());
        if (remaining_ms <= 0) break;
        int poll_ret = poll(&pfd, 1, remaining_ms);
        if (poll_ret <= 0) break;
        if (pfd.revents & (POLLERR | POLLHUP)) {
            // Try one last read
            ssize_t n = read(read_fd_, buf, sizeof(buf));
            if (n > 0) {
                stdio_buffer_.append(buf, n);
                // Fall through to parse below
            } else {
                break;
            }
        } else {
            ssize_t n = read(read_fd_, buf, sizeof(buf));
            if (n <= 0) break;
            stdio_buffer_.append(buf, n);
        }

        // Try to parse Content-Length framed message
        size_t header_end = stdio_buffer_.find("\r\n\r\n");
        if (header_end != std::string::npos) {
            size_t content_start = header_end + 4;
            size_t cl_pos = stdio_buffer_.find("Content-Length: ");
            if (cl_pos != std::string::npos && cl_pos < header_end) {
                size_t cl_end = stdio_buffer_.find("\r\n", cl_pos);
                int content_length = std::stoi(stdio_buffer_.substr(cl_pos + 16, cl_end - cl_pos - 16));
                if (static_cast<int>(stdio_buffer_.size() - content_start) >= content_length) {
                    std::string message = stdio_buffer_.substr(content_start, content_length);
                    stdio_buffer_ = stdio_buffer_.substr(content_start + content_length);
                    return message;
                }
            }
        }
    }

    return "";
}

void McpConnection::disconnect() {
    if (type_ == Type::Sse) {
        if (sse_transport_) sse_transport_->disconnect();
        sse_transport_.reset();
        return;
    }

    stdio_disconnect();
}

bool McpConnection::is_connected() const {
    if (type_ == Type::Sse) {
        return sse_transport_ && sse_transport_->is_connected();
    }
    return write_fd_ >= 0 && child_pid_ > 0;
}

void McpConnection::stdio_send(const std::string& message) {
    if (write_fd_ < 0) return;
    std::string framed = "Content-Length: " + std::to_string(message.size()) + "\r\n\r\n" + message;
    write(write_fd_, framed.data(), framed.size());
}

void McpConnection::stdio_disconnect() {
    if (child_pid_ > 0) {
        kill(child_pid_, SIGTERM);
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(3000);
        while (std::chrono::steady_clock::now() < deadline) {
            int status;
            pid_t ret = waitpid(child_pid_, &status, WNOHANG);
            if (ret == child_pid_ || ret == -1) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        kill(child_pid_, SIGKILL);
        waitpid(child_pid_, nullptr, 0);
        child_pid_ = -1;
    }

    if (write_fd_ >= 0) { close(write_fd_); write_fd_ = -1; }
    if (read_fd_ >= 0) { close(read_fd_); read_fd_ = -1; }
}

}  // namespace mcp
}  // namespace claude
