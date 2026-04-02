#pragma once

#include <string>
#include <vector>
#include <optional>
#include <map>
#include <nlohmann/json.hpp>

namespace claude {
namespace lsp {

using json = nlohmann::json;

// LSP Position
struct Position {
    int line = 0;      // 0-based
    int character = 0;  // 0-based UTF-16

    json to_json() const {
        return {{"line", line}, {"character", character}};
    }

    static Position from_json(const json& j) {
        return {j.value("line", 0), j.value("character", 0)};
    }
};

// LSP Range
struct Range {
    Position start;
    Position end;

    json to_json() const {
        return {{"start", start.to_json()}, {"end", end.to_json()}};
    }
};

// LSP Location
struct Location {
    std::string uri;
    Range range;

    json to_json() const {
        return {{"uri", uri}, {"range", range.to_json()}};
    }

    static Location from_json(const json& j) {
        return {j.value("uri", ""),
                {Position::from_json(j["range"]["start"]),
                 Position::from_json(j["range"]["end"])}};
    }
};

// LSP Hover result
struct HoverResult {
    std::string contents;     // Markdown or plain text
    std::optional<Range> range;
};

// LSP TextEdit
struct TextEdit {
    Range range;
    std::string new_text;
};

// LSP WorkspaceEdit
struct WorkspaceEdit {
    std::map<std::string, std::vector<TextEdit>> changes;  // uri -> edits
};

// LSP Diagnostic
struct Diagnostic {
    Range range;
    std::string message;
    int severity = 1;  // 1=Error, 2=Warning, 3=Info, 4=Hint
    std::string code;
    std::string source;
};

// LSP TextDocumentSyncKind
enum class TextDocumentSyncKind {
    None = 0,
    Full = 1,
    Incremental = 2
};

// LSP Initialize result (capabilities)
struct ServerCapabilities {
    TextDocumentSyncKind text_document_sync = TextDocumentSyncKind::Full;
    bool hover_provider = false;
    bool definition_provider = false;
    bool references_provider = false;
    bool rename_provider = false;
    bool diagnostic_provider = false;
};

// LSP Client - communicates with a Language Server over stdio
class LspClient {
public:
    LspClient();
    ~LspClient();

    // Start the language server process
    bool start(const std::vector<std::string>& command,
               const std::string& root_uri = "file:///tmp");

    // Stop the server
    void stop();

    // Check if running
    bool is_running() const { return running_; }

    // Initialize the LSP connection
    bool initialize();

    // Get server capabilities
    const ServerCapabilities& capabilities() const { return capabilities_; }

    // Document synchronization
    void did_open(const std::string& uri, const std::string& language_id,
                  const std::string& content, int version = 1);
    void did_change(const std::string& uri, const std::string& content, int version);
    void did_close(const std::string& uri);

    // Language features
    std::vector<Location> go_to_definition(const std::string& uri,
                                            Position position);
    std::vector<Location> find_references(const std::string& uri,
                                           Position position,
                                           bool include_declaration = true);
    std::optional<HoverResult> hover(const std::string& uri,
                                      Position position);
    std::optional<WorkspaceEdit> rename(const std::string& uri,
                                         Position position,
                                         const std::string& new_name);

    // Publish diagnostics (received from server)
    std::vector<Diagnostic> get_diagnostics(const std::string& uri) const;

    // Shutdown
    void shutdown();

private:
    // JSON-RPC communication
    json send_request(const std::string& method, const json& params);
    void send_notification(const std::string& method, const json& params);

    // Stdio transport
    bool send_message(const std::string& message);
    std::string receive_message(int timeout_ms = 5000);

    // Process ID tracking
    bool initialize_lsp(const std::vector<std::string>& command,
                        const std::string& root_uri);
    void cleanup();

    int write_fd_ = -1;
    int read_fd_ = -1;
    pid_t child_pid_ = -1;
    bool running_ = false;
    bool initialized_ = false;

    ServerCapabilities capabilities_;
    std::map<std::string, std::vector<Diagnostic>> diagnostics_;
    std::string read_buffer_;
    int next_id_ = 1;

    mutable std::mutex mutex_;
};

}  // namespace lsp
}  // namespace claude
