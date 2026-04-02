#include "protocol/lsp/lsp_client.h"
#include <spdlog/spdlog.h>
#include <sstream>
#include <cstring>
#include <sys/wait.h>
#include <sys/poll.h>
#include <unistd.h>
#include <fcntl.h>

namespace claude {
namespace lsp {

using json = nlohmann::json;

LspClient::LspClient() = default;

LspClient::~LspClient() {
    stop();
}

bool LspClient::start(const std::vector<std::string>& command,
                      const std::string& root_uri) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (running_) {
        spdlog::warn("LSP: Server already running");
        return false;
    }

    return initialize_lsp(command, root_uri);
}

void LspClient::stop() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!running_) return;

    shutdown();
    cleanup();
}

bool LspClient::initialize_lsp(const std::vector<std::string>& command,
                                const std::string& /*root_uri*/) {
    if (command.empty()) return false;

    int stdin_pipe[2] = {-1, -1};
    int stdout_pipe[2] = {-1, -1};

    if (pipe(stdin_pipe) != 0 || pipe(stdout_pipe) != 0) {
        spdlog::error("LSP: Failed to create pipes");
        return false;
    }

    pid_t pid = fork();
    if (pid < 0) {
        spdlog::error("LSP: Fork failed");
        close(stdin_pipe[0]); close(stdin_pipe[1]);
        close(stdout_pipe[0]); close(stdout_pipe[1]);
        return false;
    }

    if (pid == 0) {
        dup2(stdin_pipe[0], STDIN_FILENO);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stdout_pipe[1], STDERR_FILENO);  // Redirect stderr too
        close(stdin_pipe[0]); close(stdin_pipe[1]);
        close(stdout_pipe[0]); close(stdout_pipe[1]);

        std::vector<char*> argv;
        for (const auto& arg : command) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);

        execvp(command[0].c_str(), argv.data());
        _exit(127);
    }

    close(stdin_pipe[0]);
    close(stdout_pipe[1]);
    write_fd_ = stdin_pipe[1];
    read_fd_ = stdout_pipe[0];
    child_pid_ = pid;
    running_ = true;

    spdlog::info("LSP: Server started (pid={})", pid);

    // Initialize
    if (!initialize()) {
        spdlog::error("LSP: Initialization failed");
        cleanup();
        return false;
    }

    return true;
}

bool LspClient::initialize() {
    json params;
    params["processId"] = static_cast<int>(getpid());
    params["rootUri"] = nullptr;
    params["capabilities"] = json::object();

    auto result = send_request("initialize", params);
    if (result.is_null()) {
        spdlog::error("LSP: initialize failed - no response");
        return false;
    }

    // Parse capabilities
    auto caps = result.value("capabilities", json::object());

    auto sync = caps.value("textDocumentSync", json::object());
    if (sync.is_number()) {
        capabilities_.text_document_sync =
            static_cast<TextDocumentSyncKind>(sync.get<int>());
    } else if (sync.is_object()) {
        int kind = sync.value("change", 1);
        capabilities_.text_document_sync = static_cast<TextDocumentSyncKind>(kind);
    }

    capabilities_.hover_provider = caps.value("hoverProvider", false);
    capabilities_.definition_provider = caps.value("definitionProvider", false);
    capabilities_.references_provider = caps.value("referencesProvider", false);
    capabilities_.rename_provider = caps.value("renameProvider", false);

    auto diag_provider = caps.value("diagnosticProvider", json());
    capabilities_.diagnostic_provider = !diag_provider.is_null();

    initialized_ = true;

    // Send initialized notification
    send_notification("initialized", {});

    spdlog::info("LSP: Initialized successfully");
    return true;
}

void LspClient::shutdown() {
    if (!initialized_) return;

    send_request("shutdown", {});
    send_notification("exit", {});
    initialized_ = false;
}

void LspClient::cleanup() {
    running_ = false;
    initialized_ = false;

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
    read_buffer_.clear();
}

bool LspClient::send_message(const std::string& message) {
    if (write_fd_ < 0) return false;
    std::string framed = "Content-Length: " + std::to_string(message.size()) + "\r\n\r\n" + message;
    ssize_t n = write(write_fd_, framed.data(), framed.size());
    return n == static_cast<ssize_t>(framed.size());
}

std::string LspClient::receive_message(int timeout_ms) {
    if (read_fd_ < 0) return "";

    // Set non-blocking for timeout
    int flags = fcntl(read_fd_, F_GETFL);
    fcntl(read_fd_, F_SETFL, flags | O_NONBLOCK);

    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

    while (std::chrono::steady_clock::now() < deadline) {
        // Check if we already have a complete message in buffer
        size_t header_end = read_buffer_.find("\r\n\r\n");
        if (header_end != std::string::npos) {
            size_t cl_pos = read_buffer_.find("Content-Length: ");
            if (cl_pos != std::string::npos && cl_pos < header_end) {
                size_t cl_end = read_buffer_.find("\r\n", cl_pos);
                int content_length = std::stoi(read_buffer_.substr(cl_pos + 16, cl_end - cl_pos - 16));
                size_t content_start = header_end + 4;
                if (read_buffer_.size() >= content_start + content_length) {
                    std::string message = read_buffer_.substr(content_start, content_length);
                    read_buffer_ = read_buffer_.substr(content_start + content_length);
                    return message;
                }
            }
        }

        // Poll for more data
        struct pollfd pfd;
        pfd.fd = read_fd_;
        pfd.events = POLLIN;
        int remaining_ms = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now()).count());
        if (remaining_ms <= 0) break;

        int poll_ret = poll(&pfd, 1, remaining_ms);
        if (poll_ret <= 0) break;

        char buf[4096];
        ssize_t n = read(read_fd_, buf, sizeof(buf));
        if (n <= 0) break;
        read_buffer_.append(buf, n);
    }

    return "";
}

json LspClient::send_request(const std::string& method, const json& params) {
    json request;
    request["jsonrpc"] = "2.0";
    request["id"] = next_id_++;
    request["method"] = method;
    request["params"] = params;

    if (!send_message(request.dump())) {
        return {};
    }

    // Read response
    std::string response = receive_message(10000);
    if (response.empty()) return {};

    try {
        auto j = json::parse(response);
        if (j.contains("result")) return j["result"];
        if (j.contains("error")) {
            spdlog::warn("LSP error for '{}': {}", method, j["error"].dump());
        }
        return {};
    } catch (...) {
        return {};
    }
}

void LspClient::send_notification(const std::string& method, const json& params) {
    json notif;
    notif["jsonrpc"] = "2.0";
    notif["method"] = method;
    notif["params"] = params;

    send_message(notif.dump());
}

// Document synchronization
void LspClient::did_open(const std::string& uri, const std::string& language_id,
                          const std::string& content, int version) {
    json params;
    params["textDocument"] = {{"uri", uri}, {"languageId", language_id}, {"version", version}};
    params["textDocument"]["text"] = content;
    send_notification("textDocument/didOpen", params);
}

void LspClient::did_change(const std::string& uri, const std::string& content, int version) {
    json params;
    params["textDocument"] = {{"uri", uri}, {"version", version}};

    if (capabilities_.text_document_sync == TextDocumentSyncKind::Incremental) {
        // For incremental, send full content as a single replace-all
        // (simplified - real incremental would compute diffs)
        params["contentChanges"] = {{{"range", nullptr}, {"text", content}}};
    } else {
        params["contentChanges"] = {{{"text", content}}};
    }

    send_notification("textDocument/didChange", params);
}

void LspClient::did_close(const std::string& uri) {
    json params;
    params["textDocument"] = {{"uri", uri}};
    send_notification("textDocument/didClose", params);
}

// Language features
std::vector<Location> LspClient::go_to_definition(const std::string& uri, Position position) {
    if (!capabilities_.definition_provider) return {};

    json params;
    params["textDocument"] = {{"uri", uri}};
    params["position"] = position.to_json();

    auto result = send_request("textDocument/definition", params);

    std::vector<Location> locations;
    if (result.is_array()) {
        for (const auto& loc : result) {
            locations.push_back(Location::from_json(loc));
        }
    } else if (result.is_object()) {
        locations.push_back(Location::from_json(result));
    }
    return locations;
}

std::vector<Location> LspClient::find_references(const std::string& uri, Position position,
                                                   bool include_declaration) {
    if (!capabilities_.references_provider) return {};

    json params;
    params["textDocument"] = {{"uri", uri}};
    params["position"] = position.to_json();
    params["context"] = {{"includeDeclaration", include_declaration}};

    auto result = send_request("textDocument/references", params);

    std::vector<Location> locations;
    if (result.is_array()) {
        for (const auto& loc : result) {
            locations.push_back(Location::from_json(loc));
        }
    }
    return locations;
}

std::optional<HoverResult> LspClient::hover(const std::string& uri, Position position) {
    if (!capabilities_.hover_provider) return std::nullopt;

    json params;
    params["textDocument"] = {{"uri", uri}};
    params["position"] = position.to_json();

    auto result = send_request("textDocument/hover", params);
    if (result.is_null()) return std::nullopt;

    HoverResult hover;
    auto contents = result.value("contents", json());
    if (contents.is_string()) {
        hover.contents = contents.get<std::string>();
    } else if (contents.is_object() && contents.contains("value")) {
        hover.contents = contents["value"].get<std::string>();
    } else {
        hover.contents = contents.dump();
    }

    if (result.contains("range")) {
        auto& r = result["range"];
        hover.range = Range{Position::from_json(r["start"]), Position::from_json(r["end"])};
    }

    return hover;
}

std::optional<WorkspaceEdit> LspClient::rename(const std::string& uri, Position position,
                                                 const std::string& new_name) {
    if (!capabilities_.rename_provider) return std::nullopt;

    json params;
    params["textDocument"] = {{"uri", uri}};
    params["position"] = position.to_json();
    params["newName"] = new_name;

    auto result = send_request("textDocument/rename", params);
    if (result.is_null()) return std::nullopt;

    WorkspaceEdit edit;
    auto changes = result.value("changes", json::object());
    for (auto& [uri_key, edits] : changes.items()) {
        std::vector<TextEdit> text_edits;
        for (const auto& e : edits) {
            TextEdit te;
            auto& r = e["range"];
            te.range = Range{Position::from_json(r["start"]), Position::from_json(r["end"])};
            te.new_text = e.value("newText", "");
            text_edits.push_back(te);
        }
        edit.changes[uri_key] = std::move(text_edits);
    }

    return edit;
}

std::vector<Diagnostic> LspClient::get_diagnostics(const std::string& uri) const {
    auto it = diagnostics_.find(uri);
    if (it != diagnostics_.end()) return it->second;
    return {};
}

}  // namespace lsp
}  // namespace claude
